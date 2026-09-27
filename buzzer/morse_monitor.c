/*
 * morse_monitor.c — 数据流断流检测 + 摩尔斯电码蜂鸣器报警 (RK3562)
 *
 * 功能：订阅 zenoh active/** 数据流，检测图像(image)与点云(pointcloud)
 *       是否持续有数据。断流超过阈值后用无源蜂鸣器播摩尔斯电码：
 *         R     = 雷达数据断流      (.-.)
 *         C     = 相机数据断流      (-.-.)
 *         RC    = 两路都断流
 *       恢复后自动停止报警；持续断流每 20s 重播一次提醒。
 *
 * 设计约束：独立只读进程，只订阅不发布，不触碰 xt_camera/xt_radar/
 *           xt_gateway 任何逻辑；日志只写 /tmp，不落 /userdata。
 *
 * 用法: morse_monitor [img_timeout_ms] [pc_timeout_ms] [duty%] [line]
 *   默认: 5000 5000 50 23
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

#include "zenoh-pico.h"

#define DIT_MS 80
#define DAH_MS 240

static int g_duty = 50;
static int g_line = 23;
static long long g_last_img = 0, g_last_pc = 0;
static volatile long g_hits = 0;

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* ---------------- 蜂鸣器（忙等方波） ---------------- */
static int open_line(int line) {
    int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) { perror("open gpiochip0"); return -1; }
    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = line;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.lines = 1;
    strncpy(req.consumer_label, "morse_monitor", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL"); close(fd); return -1;
    }
    close(fd);
    return req.fd;
}

static void tone(int lfd, int dur_ms) {
    const int freq = 1047;              /* C6 */
    long long period = (long long)(1e9 / (double)freq);
    long long hi = period * g_duty / 100; if (hi < 1) hi = 1;
    long long lo = period - hi;          if (lo < 1) lo = 1;
    struct gpiohandle_data d;
    long long tick = now_ns();
    long long end = tick + (long long)dur_ms * 1000000LL;
    while (tick < end) {
        memset(&d, 0, sizeof(d)); d.values[0] = 1;
        ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d);
        tick += hi; while (now_ns() < tick) ;
        if (tick >= end) break;
        memset(&d, 0, sizeof(d)); d.values[0] = 0;
        ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d);
        tick += lo; while (now_ns() < tick) ;
    }
    memset(&d, 0, sizeof(d)); d.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d);
}

static void silent(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ---------------- 摩尔斯 ---------------- */
static const char *morse_code(char c) {
    static const char *tbl[] = {
        ".-","-...","-.-.","-..",".","..-.","--.","....","..",".---",
        "-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-",
        "..-","...-",".--","-..-","-.--","--..",
        "-----",".----","..---","...--","....-",".....","-....","--...","---..","----."
    };
    if (c >= 'A' && c <= 'Z') return tbl[c - 'A'];
    if (c >= '0' && c <= '9') return tbl[26 + (c - '0')];
    return NULL;
}

static void morse_play(int lfd, const char *text) {
    for (const char *p = text; *p; p++) {
        char c = (char)toupper((unsigned char)*p);
        const char *m = morse_code(c);
        if (c == ' ') { silent(7 * DIT_MS); continue; }
        if (!m) continue;
        for (const char *s = m; *s; s++) {
            tone(lfd, *s == '.' ? DIT_MS : DAH_MS);
            silent(DIT_MS);
        }
        silent(3 * DIT_MS - DIT_MS);
    }
    silent(3 * DIT_MS);                 /* 结尾缓冲 */
}

/* ---------------- zenoh 订阅 ---------------- */
static void data_handler(z_loaned_sample_t *sample, void *arg) {
    (void)arg;
    const z_loaned_keyexpr_t *ke = z_sample_keyexpr(sample);
    z_view_string_t kstr;
    if (z_keyexpr_as_view_string(ke, &kstr) != Z_OK) return;
    const char *k = z_string_data(z_loan(kstr));
    size_t klen = z_string_len(z_loan(kstr));

    g_hits++;
    if (klen >= 6 && strcmp(k + klen - 6, "/image") == 0)
        g_last_img = now_ms();
    else if (klen >= 11 && strcmp(k + klen - 11, "/pointcloud") == 0)
        g_last_pc = now_ms();
}

/* ---------------- 入口 ---------------- */
int main(int argc, char **argv) {
    long img_timeout = 5000, pc_timeout = 5000;
    if (argc > 1) img_timeout = atol(argv[1]);
    if (argc > 2) pc_timeout = atol(argv[2]);
    if (argc > 3) g_duty = atoi(argv[3]);
    if (argc > 4) g_line = atoi(argv[4]);
    if (img_timeout < 1000) img_timeout = 1000;
    if (pc_timeout < 1000) pc_timeout = 1000;
    if (g_duty < 2) g_duty = 2;
    if (g_duty > 50) g_duty = 50;
    if (g_line < 0 || g_line > 31) g_line = 23;

    int lfd = open_line(g_line);
    if (lfd < 0) return 1;

    /* zenoh: client 模式直连本机 router */
    z_owned_config_t cfg;
    z_config_default(&cfg);
    zp_config_insert(z_loan(cfg), Z_CONFIG_MODE_KEY, "client");
    zp_config_insert(z_loan(cfg), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:7447");

    z_owned_session_t s;
    z_open_options_t opts;
    z_open_options_default(&opts);
    if (z_open(&s, z_move(cfg), &opts) != Z_OK) {
        printf("[monitor] zenoh open failed\n");
        return 1;
    }
    z_owned_closure_sample_t cb;
    z_closure(&cb, data_handler, NULL, NULL);
    z_owned_subscriber_t sub;
    z_view_keyexpr_t ke;
    if (z_view_keyexpr_from_str(&ke, "active/**") < 0) {
        printf("[monitor] bad keyexpr\n");
        return 1;
    }
    if (z_declare_subscriber(z_loan(s), &sub, z_loan(ke), z_move(cb), NULL) < 0) {
        printf("[monitor] declare failed\n");
        return 1;
    }

    /* 启动时刻作为基准，避免冷启动误报 */
    g_last_img = g_last_pc = now_ms();

    printf("[monitor] started: img_to=%ldms pc_to=%ldms duty=%d%% line=%d\n",
           img_timeout, pc_timeout, g_duty, g_line);

    long last_alarm = 0;
    int last_state = 0;
    for (;;) {
        z_sleep_s(1);                   /* 驱动 zenoh 回调 */
        printf("[dbg] t=%lld img=%lld pc=%lld hits=%d\n", now_ms(), g_last_img, g_last_pc, g_hits);
        fflush(stdout);
        long long nw = now_ms();
        int img_ok = (nw - g_last_img) < img_timeout;
        int pc_ok  = (nw - g_last_pc)  < pc_timeout;
        int state = (pc_ok ? 0 : 1) | (img_ok ? 0 : 2);   /* bit1=雷达 bit2=相机 */
        const char *code = state == 1 ? "R" : state == 2 ? "C" : "RC";

        if (state != last_state) {
            last_state = state;
            last_alarm = nw;
            if (state) {
                printf("[alarm] %s lost -> %s\n", state == 3 ? "radar+camera" :
                       state == 1 ? "radar" : "camera", code);
                fflush(stdout);
                morse_play(lfd, code);
            } else {
                printf("[ok] all streams recovered\n");
                fflush(stdout);
            }
        } else if (state && nw - last_alarm > 20000) {
            last_alarm = nw;
            printf("[alarm-repeat] %s\n", code);
            fflush(stdout);
            morse_play(lfd, code);
        }
    }
    return 0;
}
