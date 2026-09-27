/* vizd v3 - 可视化服务
 *  :8081
 *    /camera  MJPEG multipart/x-mixed-replace (单 ffmpeg 常驻, 帧广播, 任意连接数)
 *    /radar   XTGR 雷达点云 2D 网格 (UDP :17778 镜像 + protobuf 解析)
 *    /health  ok
 * 编译: aarch64-linux-gnu-gcc -O2 -static -pthread -o vizd_arm vizd3.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>

#define VIZD_PORT   8081
#define UDP_PORT    17778
#define MAX_CONN    64

static const char *RTSP_URL =
    "rtsp://192.0.2.123:554/stream0?username=admin&password=123456";
static const char *FFMPEG = "/userdata/xtapp/ffmpeg";

/* ---------------- 相机帧广播 ---------------- */
static pthread_mutex_t g_fm = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *g_frame = NULL;
static size_t   g_frame_len = 0;
static uint32_t g_frame_seq = 0;

static void *ffmpeg_thread(void *arg)
{
    (void)arg;
    uint8_t *buf = malloc(2 * 1024 * 1024);
    if (!buf) return NULL;
    while (1) {
        int fds[2];
        if (pipe(fds) != 0) { sleep(2); continue; }
        pid_t pid = fork();
        if (pid == 0) {
            dup2(fds[1], 1);
            close(fds[0]); close(fds[1]);
            execl(FFMPEG, "ffmpeg",
                  "-hide_banner", "-loglevel", "error",
                  "-rtsp_transport", "tcp", "-i", RTSP_URL,
                  "-c:v", "mjpeg", "-q:v", "6", "-an", "-r", "8",
                  "-vf", "scale=960:540", "-f", "image2pipe", "pipe:1", NULL);
            _exit(127);
        }
        close(fds[1]);
        size_t start = 0, len = 0;
        while (1) {
            ssize_t n = read(fds[0], buf + len, 2 * 1024 * 1024 - len);
            if (n <= 0) break;
            len += (size_t)n;
            size_t i = (start ? start : 1);
            while (i + 1 < len) {
                if (buf[i] == 0xFF && buf[i+1] == 0xD8) {
                    if (i > start) {
                        size_t fl = i - start;
                        while (fl > 2 && buf[start+fl-1] == '\n') fl--;
                        if (fl > 100) {
                            pthread_mutex_lock(&g_fm);
                            free(g_frame);
                            g_frame = malloc(fl);
                            if (g_frame) {
                                memcpy(g_frame, buf + start, fl);
                                g_frame_len = fl;
                                g_frame_seq++;
                            }
                            pthread_mutex_unlock(&g_fm);
                        }
                    }
                    start = i;
                    i += 2;
                } else {
                    i++;
                }
            }
            if (start > 0 && len - start > 64 * 1024) {
                memmove(buf, buf + start, len - start);
                len -= start;
                start = 0;
            }
        }
        kill(pid, SIGKILL);
        close(fds[0]);
        pthread_mutex_lock(&g_fm);
        free(g_frame); g_frame = NULL; g_frame_len = 0;
        pthread_mutex_unlock(&g_fm);
        sleep(2);
    }
    return NULL;
}

static void serve_camera(int cfd)
{
    char hdr[300];
    int hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=--vizframe\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-cache\r\nConnection: close\r\n\r\n");
    if (write(cfd, hdr, hn) <= 0) { close(cfd); return; }

    uint8_t *f = NULL;
    size_t fl = 0;
    uint32_t last = 0;
    while (1) {
        pthread_mutex_lock(&g_fm);
        if (g_frame && g_frame_len && g_frame_seq != last) {
            free(f);
            f = malloc(g_frame_len);
            if (f) { memcpy(f, g_frame, g_frame_len); fl = g_frame_len; last = g_frame_seq; }
            else fl = 0;
        } else {
            fl = 0;
        }
        pthread_mutex_unlock(&g_fm);
        if (fl == 0) { usleep(120000); continue; }

        char bh[160];
        int bn = snprintf(bh, sizeof(bh),
            "--vizframe\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", fl);
        if (write(cfd, bh, bn) < 0 || write(cfd, f, fl) < 0 ||
            write(cfd, "\r\n", 2) < 0) {
            break;
        }
        usleep(100000);
    }
    free(f);
    close(cfd);
}

/* ---------------- 雷达网格 ---------------- */
#define GRID_W      160
#define GRID_H      80
#define GRID_X_MIN  0.0f
#define GRID_X_MAX  2.0f
#define GRID_Y_MIN  -1.0f
#define GRID_Y_MAX  1.0f

static uint8_t  g_grid[GRID_W * GRID_H];
static uint32_t g_grid_seq = 0;
static int      g_grid_have = 0;
static pthread_mutex_t g_gm = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    const uint8_t *d;
    size_t n, pos;
    int err;
} PBReader;

static uint64_t pb_varint(PBReader *r)
{
    uint64_t v = 0; int sh = 0;
    while (r->pos < r->n && sh < 64) {
        uint8_t b = r->d[r->pos++];
        v |= (uint64_t)(b & 0x7f) << sh;
        if (!(b & 0x80)) return v;
        sh += 7;
    }
    r->err = 1;
    return 0;
}

static int64_t pb_zigzag(uint64_t v) { return (int64_t)(v >> 1) ^ -(int64_t)(v & 1); }

static void pc_render(const uint8_t *data, size_t len)
{
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
    uint8_t grid[GRID_W * GRID_H];
    memset(grid, 0, sizeof(grid));
    int64_t scaler = 1000;
    int have = 0;
    while (r.pos < r.n) {
        uint64_t tag = pb_varint(&r);
        if (r.err) break;
        int f = (int)(tag >> 3), w = (int)(tag & 7);
        if (f == 1 && w == 2) {
            uint64_t hl = pb_varint(&r);
            if (r.err) break;
            PBReader h = { r.d, hl, 0, 0 };
            r.pos += hl;
            while (h.pos < h.n) {
                uint64_t ht = pb_varint(&h);
                if (h.err) break;
                int hf = (int)(ht >> 3), hw = (int)(ht & 7);
                if (hw == 0) {
                    uint64_t v = pb_varint(&h);
                    if (!h.err && hf == 3) scaler = pb_zigzag(v);
                } else if (hw == 2) {
                    uint64_t sl = pb_varint(&h); h.pos += sl;
                } else { h.err = 1; }
            }
        } else if (f == 2 && w == 2) {
            uint64_t pl = pb_varint(&r);
            if (r.err || pl > r.n - r.pos) break;
            PBReader p = { r.d, (size_t)pl, 0, 0 };
            r.pos += pl;
            int64_t x = 0, y = 0;
            int has_xy = 0;
            uint32_t rgbi = 0;
            while (p.pos < p.n) {
                uint64_t pt = pb_varint(&p);
                if (p.err) break;
                int pf = (int)(pt >> 3), pw = (int)(pt & 7);
                if (pw == 0) {
                    uint64_t v = pb_varint(&p);
                    if (p.err) break;
                    if (pf == 1) { x = pb_zigzag(v); has_xy |= 1; }
                    else if (pf == 2) { y = pb_zigzag(v); has_xy |= 2; }
                    else if (pf == 3) { }
                    else if (pf == 4) rgbi = (uint32_t)v;
                } else if (pw == 2) {
                    uint64_t sl = pb_varint(&p); p.pos += sl;
                } else { p.err = 1; }
            }
            if (has_xy == 3 && scaler != 0) {
                float fx = (float)x / (float)scaler;
                float fy = (float)y / (float)scaler;
                int gx = (int)((fx - GRID_X_MIN) / (GRID_X_MAX - GRID_X_MIN) * GRID_W);
                int gy = (int)((fy - GRID_Y_MIN) / (GRID_Y_MAX - GRID_Y_MIN) * GRID_H);
                if (gx >= 0 && gx < GRID_W && gy >= 0 && gy < GRID_H) {
                    uint8_t v = (rgbi > 255) ? 255 : (uint8_t)rgbi;
                    if (v > grid[gy * GRID_W + gx]) grid[gy * GRID_W + gx] = v;
                    have = 1;
                }
            }
        } else if (w == 0) {
            pb_varint(&r);
        } else if (w == 2) {
            uint64_t sl = pb_varint(&r); r.pos += sl;
        } else {
            break;
        }
    }
    if (have) {
        pthread_mutex_lock(&g_gm);
        memcpy(g_grid, grid, sizeof(grid));
        g_grid_seq++;
        g_grid_have = 1;
        pthread_mutex_unlock(&g_gm);
    }
}

static void *udp_thread(void *arg)
{
    (void)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return NULL;
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(UDP_PORT);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(fd);
        return NULL;
    }
    uint8_t buf[65536];
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    while (1) {
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&from, &fl);
        if (n <= 0) continue;
        if (n < 24 || memcmp(buf, "XTPC", 4) != 0) continue;
        uint32_t seq, total, idx, cnt;
        memcpy(&seq, buf + 4, 4); memcpy(&total, buf + 8, 4);
        memcpy(&idx, buf + 12, 4); memcpy(&cnt, buf + 16, 4);
        static uint32_t last_seq = 0xFFFFFFFF;
        static uint8_t  *acc = NULL;
        static size_t    acc_len = 0, acc_cnt = 0;
        if (seq != last_seq) {
            free(acc); acc = NULL; acc_len = 0; acc_cnt = 0;
            last_seq = seq;
        }
        if (total > 4 * 1024 * 1024) continue;
        if (!acc) acc = malloc(total);
        if (!acc) continue;
        size_t off = (size_t)idx * 1200;
        size_t cl = (size_t)(n - 24);
        if (off + cl > total) cl = total - off;
        memcpy(acc + off, buf + 24, cl);
        acc_cnt++;
        if (acc_cnt >= cnt) {
            pc_render(acc, total);
            free(acc); acc = NULL;
            acc_len = 0; acc_cnt = 0;
            last_seq = 0xFFFFFFFF;
        }
    }
    return NULL;
}

static void serve_radar(int cfd)
{
    uint8_t grid[GRID_W * GRID_H];
    uint32_t seq;
    pthread_mutex_lock(&g_gm);
    int have = g_grid_have;
    if (have) {
        memcpy(grid, g_grid, sizeof(grid));
        seq = g_grid_seq;
    }
    pthread_mutex_unlock(&g_gm);

    char hdr[300];
    if (!have) {
        int hn = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Length: 8\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n"
            "XTGRNODATA");
        write(cfd, hdr, hn);
        close(cfd);
        return;
    }
    int hn = snprintf(hdr, sizeof(hdr),
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

static void serve_health(int cfd)
{
    char hdr[200];
    int hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: 2\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\nok");
    write(cfd, hdr, hn);
    close(cfd);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, ffmpeg_thread, NULL);
    pthread_create(&t2, NULL, udp_thread, NULL);

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) return 1;
    int on = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(VIZD_PORT);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(sfd, MAX_CONN) < 0) { perror("listen"); return 1; }

    printf("vizd listening on :%d (radar udp :%d)\n", VIZD_PORT, UDP_PORT);
    fflush(stdout);

    while (1) {
        struct sockaddr_in csa;
        socklen_t cl = sizeof(csa);
        int cfd = accept(sfd, (struct sockaddr *)&csa, &cl);
        if (cfd < 0) { usleep(100000); continue; }

        char buf[512];
        ssize_t n = read(cfd, buf, sizeof(buf) - 1);
        if (n <= 0) { close(cfd); continue; }
        buf[n] = 0;

        if (strncmp(buf, "GET /camera", 11) == 0) {
            serve_camera(cfd);
        } else if (strncmp(buf, "GET /radar", 10) == 0) {
            serve_radar(cfd);
        } else if (strncmp(buf, "GET /health", 11) == 0) {
            serve_health(cfd);
        } else {
            char r[160];
            int rn = snprintf(r, sizeof(r),
                "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            write(cfd, r, rn);
            close(cfd);
        }
    }
    return 0;
}
