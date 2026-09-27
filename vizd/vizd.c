/* vizd.c - 轻量可视化服务 (RK3562 板端) v2
 * 监听 0.0.0.0:8081
 *   GET /camera  -> MJPEG 流 (multipart/x-mixed-replace), 数据源: ffmpeg 拉相机 RTSP 转 JPEG
 *   GET /radar   -> 点云 2D 网格热力图 (原始字节, 前端 canvas 渲染)
 *   GET /        -> 说明页
 *   GET /health  -> ok
 * 雷达数据源: xt_radar 的 XT_PC_UDP_MIRROR=1 UDP 分片(目标 192.0.2.200:17778, 经 iptables REDIRECT 到本机)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT        8081
#define BOUNDARY    "--vizframe"
#define MAXBUF      (1 << 20)   /* MJPEG 帧缓冲上限 1MB */

#define UDP_PORT    17778
#define XTPC_MAGIC  0x43505458  /* "XTPC" little-endian */
#define CHUNK_MAX   1200
#define PC_MAXFRAME (1 << 21)   /* 点云单帧最大 2MB */

/* 雷达网格参数 (俯视图, x=前后, y=左右) */
#define GRID_W      160
#define GRID_H      80
#define GRID_X_MIN  0.0f    /* x 范围 0..2m (XT-M60 近距离) */
#define GRID_X_MAX  2.0f
#define GRID_Y_MIN  -1.0f   /* y 范围 ±1m */
#define GRID_Y_MAX  1.0f

static const char *RTSP_URL =
    "rtsp://192.0.2.123:554/stream0?username=admin&password=123456";

/* ---------------- 雷达网格共享状态 ---------------- */
static uint8_t  g_grid[GRID_W * GRID_H];
static uint32_t g_grid_seq = 0;
static int      g_grid_have = 0;

/* ---------------- protobuf 读取器 ---------------- */
typedef struct {
    const uint8_t *p;
    size_t len;
    size_t pos;
    int err;
} PBReader;

static uint64_t pb_varint(PBReader *r) {
    uint64_t v = 0; int sh = 0;
    while (r->pos < r->len && sh < 64) {
        uint8_t b = r->p[r->pos++];
        v |= (uint64_t)(b & 0x7f) << sh;
        if (!(b & 0x80)) return v;
        sh += 7;
    }
    r->err = 1; return 0;
}
static int64_t pb_zigzag(uint64_t v) {
    return (int64_t)(v >> 1) ^ -(int64_t)(v & 1);
}

/* ---------------- XTPC UDP 分片重组 ---------------- */
typedef struct {
    uint8_t *buf;
    uint32_t total_len;
    uint32_t chunk_count;
    uint32_t *seen;   /* bitset 简化为计数+已收集合 */
    uint8_t *got;
} XtpFrame;

static XtpFrame g_xtp;
static pthread_mutex_t g_grid_mtx = PTHREAD_MUTEX_INITIALIZER;

static void xtp_reset(XtpFrame *f) {
    free(f->buf); free(f->got);
    memset(f, 0, sizeof(*f));
}

static int xtp_ingest(XtpFrame *f, const uint8_t *pkt, size_t n) {
    if (n < 24) return 0;
    uint32_t magic, seq, total_len, idx, chunk_count, clen;
    memcpy(&magic, pkt, 4);
    memcpy(&seq, pkt + 4, 4);
    memcpy(&total_len, pkt + 8, 4);
    memcpy(&idx, pkt + 12, 4);
    memcpy(&chunk_count, pkt + 16, 4);
    memcpy(&clen, pkt + 20, 4);
    if (magic != XTPC_MAGIC || idx >= chunk_count || chunk_count == 0) return 0;
    if (total_len == 0 || total_len > PC_MAXFRAME) return 0;
    if (clen > n - 24) return 0;
    if (clen > CHUNK_MAX) return 0;
    if (idx == 0 || f->total_len != total_len) {
        xtp_reset(f);
        f->buf = malloc(total_len);
        f->got = calloc(1, chunk_count);
        if (!f->buf || !f->got) { xtp_reset(f); return 0; }
        f->total_len = total_len;
        f->chunk_count = chunk_count;
    }
    if (f->got[idx]) return 0;
    memcpy(f->buf + (size_t)idx * CHUNK_MAX, pkt + 24, clen);
    f->got[idx] = 1;
    /* 检查是否完整 */
    for (uint32_t i = 0; i < f->chunk_count; i++)
        if (!f->got[i]) return 0;
    return 1;
}

/* ---------------- 点云 protobuf 解析 -> 网格 ---------------- */
static void pc_render(const uint8_t *data, size_t len) {
    /* 外层数组包装: 单个 field1 wire2 -> 内层 LidarPointMsg */
    if (len > 4 && data[0] == 0x0a && (data[1] & 0x80)) {
        PBReader ow = { data, len, 0, 0 };
        uint64_t t0 = pb_varint(&ow);
        if (!ow.err && (t0 >> 3) == 1 && (t0 & 7) == 2) {
            uint64_t ol = pb_varint(&ow);
            if (!ow.err && ol > 2 && ol <= len - ow.pos && data[ow.pos] == 0x0a) {
                data = data + ow.pos;
                len = (size_t)ol;
            }
        }
    }
    PBReader r = { data, len, 0, 0 };
    int have_scaler = 0;
    int64_t scaler = 1000;
    uint8_t grid[GRID_W * GRID_H];
    memset(grid, 0, sizeof(grid));
    int pts = 0;

    while (r.pos < r.len && !r.err) {
        uint64_t tag = pb_varint(&r);
        if (r.err) break;
        int field = (int)(tag >> 3);
        int wire = (int)(tag & 7);
        if (field == 1 && wire == 2) {          /* header (嵌套) */
            uint64_t hl = pb_varint(&r);
            if (r.err || hl > r.len - r.pos) { r.err = 1; break; }
            PBReader h = { r.p + r.pos, (size_t)hl, 0, 0 };
            r.pos += (size_t)hl;
            while (h.pos < h.len && !h.err) {
                uint64_t ht = pb_varint(&h);
                int hf = (int)(ht >> 3), hw = (int)(ht & 7);
                if (hw == 0) {
                    uint64_t v = pb_varint(&h);
                    if (h.err) break;
                    if (hf == 3) { scaler = pb_zigzag(v); have_scaler = 1; }
                } else if (hw == 2) {
                    uint64_t sl = pb_varint(&h);
                    if (h.err || sl > h.len - h.pos) { h.err = 1; break; }
                    h.pos += (size_t)sl;
                } else { r.err = 1; break; }
            }
        } else if (field == 2 && wire == 2) {   /* points 元素 (嵌套 LidarPoint) */
            uint64_t pl = pb_varint(&r);
            if (r.err || pl > r.len - r.pos) { r.err = 1; break; }
            PBReader pt = { r.p + r.pos, (size_t)pl, 0, 0 };
            r.pos += (size_t)pl;
            int64_t x = 0, y = 0, z = 0;
            uint32_t rgbi = 0;
            int have_xy = 0;
            while (pt.pos < pt.len && !pt.err) {
                uint64_t pt_t = pb_varint(&pt);
                int pf = (int)(pt_t >> 3), pw = (int)(pt_t & 7);
                if (pw == 0) {
                    uint64_t v = pb_varint(&pt);
                    if (pt.err) break;
                    if (pf == 1) { x = pb_zigzag(v); have_xy |= 1; }
                    else if (pf == 2) { y = pb_zigzag(v); have_xy |= 2; }
                    else if (pf == 3) { z = pb_zigzag(v); }
                    else if (pf == 4) { rgbi = (uint32_t)v; }
                } else if (pw == 2) {
                    uint64_t sl = pb_varint(&pt);
                    if (pt.err || sl > pt.len - pt.pos) { pt.err = 1; break; }
                    pt.pos += (size_t)sl;
                } else { pt.err = 1; break; }
            }
            if (pt.err) break;
            (void)z;
            if (have_xy == 3) {
                float fx = (float)x / (float)scaler;
                float fy = (float)y / (float)scaler;
                int gx = (int)((fx - GRID_X_MIN) / (GRID_X_MAX - GRID_X_MIN) * GRID_W);
                int gy = (int)((fy - GRID_Y_MIN) / (GRID_Y_MAX - GRID_Y_MIN) * GRID_H);
                if (gx >= 0 && gx < GRID_W && gy >= 0 && gy < GRID_H) {
                    int idx = gy * GRID_W + gx;
                    uint8_t v = (uint8_t)(rgbi > 255 ? 255 : rgbi);
                    if (v > grid[idx]) grid[idx] = v;
                    pts++;
                }
            }
        } else if (wire == 0) {
            pb_varint(&r);
        } else if (wire == 2) {
            uint64_t sl = pb_varint(&r);
            if (r.err || sl > r.len - r.pos) { r.err = 1; break; }
            r.pos += (size_t)sl;
        } else { r.err = 1; break; }
    }
    if (r.err) return;
    (void)have_scaler; (void)pts;
    pthread_mutex_lock(&g_grid_mtx);
    memcpy(g_grid, grid, sizeof(grid));
    g_grid_seq++;
    g_grid_have = 1;
    pthread_mutex_unlock(&g_grid_mtx);
}

/* ---------------- UDP 接收线程 ---------------- */
static void *udp_loop(void *arg) {
    (void)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("udp socket"); return NULL; }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(UDP_PORT);
    if (bind(fd, (struct sockaddr*)&a, sizeof(a)) < 0) { perror("udp bind"); close(fd); return NULL; }
    uint8_t pkt[24 + CHUNK_MAX];
    for (;;) {
        ssize_t n = recv(fd, pkt, sizeof(pkt), 0);
        if (n < 24) continue;
        if (xtp_ingest(&g_xtp, pkt, (size_t)n)) {
            pc_render(g_xtp.buf, g_xtp.total_len);
        }
    }
    return NULL;
}

/* ---------------- HTTP ---------------- */
static void http_reply(int fd, int code, const char *ct, const char *body) {
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-cache\r\nConnection: close\r\n\r\n",
        code, code==200?"OK":"Not Found", ct, body?strlen(body):0);
    if (write(fd, hdr, n) < 0) return;
    if (body) write(fd, body, strlen(body));
}

static void pump_frames(int cfd, int pfd) {
    unsigned char *buf = malloc(MAXBUF);
    if (!buf) return;
    size_t start = 0, len = 0;
    ssize_t r;
    while ((r = read(pfd, buf + len, MAXBUF - len - 1)) > 0) {
        len += (size_t)r;
        size_t i = start;
        while (i + 1 < len) {
            if (buf[i] == 0xff && buf[i+1] == 0xd8) {
                if (i > start) {
                    size_t flen = i - start;
                    char hdr[128];
                    int hn = snprintf(hdr, sizeof(hdr),
                        "--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                        BOUNDARY, flen);
                    if (write(cfd, hdr, hn) < 0) goto done;
                    if (write(cfd, buf + start, flen) < 0) goto done;
                    if (write(cfd, "\r\n", 2) < 0) goto done;
                }
                start = i; i += 2;
            } else i++;
        }
        if (start > 0) {
            memmove(buf, buf + start, len - start);
            len -= start; start = 0;
        }
        if (len >= MAXBUF - 1) { len = 0; start = 0; }
    }
done:
    if (len > start && len - start > 1) {
        size_t flen = len - start;
        char hdr[128];
        int hn = snprintf(hdr, sizeof(hdr),
            "--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
            BOUNDARY, flen);
        if (write(cfd, hdr, hn) > 0 && write(cfd, buf + start, flen) > 0)
            write(cfd, "\r\n", 2);
    }
    free(buf);
}

static void serve_camera(int cfd) {
    int p[2];
    if (pipe(p) != 0) { close(cfd); return; }
    pid_t pid = fork();
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], STDOUT_FILENO);
        close(p[1]);
        execl("/userdata/xtapp/ffmpeg", "ffmpeg",
              "-hide_banner", "-loglevel", "error",
              "-rtsp_transport", "tcp", "-i", RTSP_URL,
              "-c:v", "mjpeg", "-q:v", "6", "-an", "-r", "8",
              "-vf", "scale=960:540",
              "-f", "image2pipe", "pipe:1", (char*)NULL);
        _exit(1);
    }
    close(p[1]);
    dprintf(cfd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=%s\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n\r\n", BOUNDARY);
    pump_frames(cfd, p[0]);
    close(p[0]);
    close(cfd);
    int st;
    waitpid(pid, &st, 0);
}

static void serve_radar(int cfd) {
    uint8_t grid[GRID_W * GRID_H];
    uint32_t seq;
    pthread_mutex_lock(&g_grid_mtx);
    memcpy(grid, g_grid, sizeof(grid));
    seq = g_grid_seq;
    int have = g_grid_have;
    pthread_mutex_unlock(&g_grid_mtx);
    char hdr[160];
    int hn;
    if (!have) {
        hn = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Length: 8\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n"
            "XTGRNODATA");
        write(cfd, hdr, hn);
        close(cfd);
        return;
    }
    hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %zu\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n",
        4 + 2 + 2 + 4 + 16 + sizeof(grid));
    write(cfd, hdr, hn);
    write(cfd, "XTGR", 4);
    uint8_t meta[8];
    meta[0] = (uint8_t)GRID_W; meta[1] = (uint8_t)GRID_H;
    meta[2] = (uint8_t)(seq >> 0); meta[3] = (uint8_t)(seq >> 8);
    meta[4] = (uint8_t)(seq >> 16); meta[5] = (uint8_t)(seq >> 24);
    meta[6] = 0; meta[7] = 0;
    write(cfd, meta, 8);
    float rng[4] = { GRID_X_MIN, GRID_X_MAX, GRID_Y_MIN, GRID_Y_MAX };
    write(cfd, rng, 16);
    write(cfd, grid, sizeof(grid));
    close(cfd);
}

static void handle_client(int cfd) {
    char req[1024];
    ssize_t n = read(cfd, req, sizeof(req) - 1);
    if (n <= 0) { close(cfd); return; }
    req[n] = 0;
    char method[16], path[512];
    if (sscanf(req, "%15s %511s", method, path) != 2) { close(cfd); return; }

    if (strcmp(path, "/camera") == 0) { serve_camera(cfd); return; }
    if (strcmp(path, "/radar") == 0)  { serve_radar(cfd); return; }
    if (strcmp(path, "/health") == 0) {
        http_reply(cfd, 200, "text/plain", "ok\n");
        close(cfd); return;
    }
    http_reply(cfd, 200, "text/html; charset=utf-8",
        "<html><head><title>XT 可视化服务</title></head><body>"
        "<h2>XT 可视化服务</h2>"
        "<p><a href=\"/camera\">相机 MJPEG</a></p>"
        "<p><a href=\"/radar\">雷达点云网格</a></p>"
        "<img src=\"/camera\" style=\"max-width:96vw\">"
        "</body></html>");
    close(cfd);
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    pthread_t tid;
    if (pthread_create(&tid, NULL, udp_loop, NULL) != 0)
        fprintf(stderr, "udp thread create failed\n");
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) { perror("socket"); return 1; }
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(PORT);
    if (bind(ls, (struct sockaddr*)&a, sizeof(a)) < 0) { perror("bind"); return 1; }
    if (listen(ls, 8) < 0) { perror("listen"); return 1; }
    printf("vizd listening on :%d (radar udp :%d)\n", PORT, UDP_PORT);
    fflush(stdout);
    for (;;) {
        int cfd = accept(ls, NULL, NULL);
        if (cfd < 0) { if (errno == EINTR) continue; break; }
        pid_t pid = fork();
        if (pid == 0) {
            close(ls);
            handle_client(cfd);
            _exit(0);
        }
        close(cfd);
    }
    return 0;
}
