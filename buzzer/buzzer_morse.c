/*
 * buzzer_morse.c — 摩尔斯电码蜂鸣器播放器 (RK3562 / Linux GPIO line23)
 *
 * 用法: buzzer_morse <text> [duty%] [line]
 *   文本支持 A-Z 0-9 空格（单词间隔 7 dit）；小写自动转大写。
 *   例: buzzer_morse RC        -> 雷达+相机双断
 *       buzzer_morse "SOS OK"
 * 时间基准: dit=80ms dah=240ms；字母间隔 3 dit，单词间隔 7 dit。
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

#define DIT_MS 80
#define DAH_MS 240

static int g_duty = 50;

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static const char *morse_code(char c) {
    static const char *tbl[] = {
        /* A-Z */
        ".-","-...","-.-.","-..",".","..-.","--.","....","..",".---",
        "-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-",
        "..-","...-",".--","-..-","-.--","--..",
        /* 0-9 */
        "-----",".----","..---","...--","....-",".....","-....","--...","---..","----."
    };
    if (c >= 'A' && c <= 'Z') return tbl[c - 'A'];
    if (c >= '0' && c <= '9') return tbl[26 + (c - '0')];
    return NULL;
}

static int open_line(int line) {
    int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) { perror("open gpiochip0"); return -1; }
    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = line;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.lines = 1;
    strncpy(req.consumer_label, "buzzer_morse", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL"); close(fd); return -1;
    }
    close(fd);
    return req.fd;
}

/* 忙等方波输出单音 */
static void tone(int lfd, int dur_ms) {
    const int freq = 1047;              /* C6，蜂鸣器舒适区 */
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

int main(int argc, char **argv) {
    int line = 23;
    if (argc < 2) {
        printf("usage: buzzer_morse <text> [duty%%] [line]\n");
        return 1;
    }
    if (argc > 2) g_duty = atoi(argv[2]);
    if (argc > 3) line = atoi(argv[3]);
    if (g_duty < 2) g_duty = 2;
    if (g_duty > 50) g_duty = 50;
    if (line < 0 || line > 31) line = 23;

    int lfd = open_line(line);
    if (lfd < 0) return 1;

    printf("buzzer_morse text=%s duty=%d%% line=%d\n", argv[1], g_duty, line);
    for (const char *p = argv[1]; *p; p++) {
        char c = (char)toupper((unsigned char)*p);
        const char *m = morse_code(c);
        if (c == ' ') {
            silent(7 * DIT_MS);                 /* 单词间隔 */
            continue;
        }
        if (!m) continue;                       /* 忽略其他字符 */
        for (const char *s = m; *s; s++) {
            tone(lfd, *s == '.' ? DIT_MS : DAH_MS);
            silent(DIT_MS);                     /* 符号间隔 */
        }
        silent(3 * DIT_MS - DIT_MS);            /* 字母间隔(补足) */
    }
    close(lfd);
    printf("done\n");
    return 0;
}
