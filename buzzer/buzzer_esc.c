/*
 * buzzer_esc.c — BLHeli_32 ESC 电调提示音测试 (RK3562 / Linux GPIO line23)
 *
 * 记谱（用户提供，X8/Y6/*8 等机型）：
 *   ESC1: C61/4 D61/4 G61/2 D61/4 G61/2      GEN Length 13  GEN Interval 2
 *   ESC2..8: C61/4 D61/4 G61/2
 * 解析：1/4=四分时值 260ms，1/2=二分时值 520ms，段间 900ms 静音。
 * C6=1047Hz D6=1175Hz G6=1568Hz，50% 占空比方波。
 *
 * 使用: buzzer_esc [duty%] [line]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <time.h>

static int midi_to_freq(int midi) {
    if (midi <= 0) return 0;
    return (int)lround(440.0 * pow(2.0, (double)(midi - 69) / 12.0));
}

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int g_duty = 50;

/* 忙等翻转方波，ns 绝对游标对齐，避免调度抖动 */
static void tone(int lfd, int freq, int dur_ms) {
    if (freq <= 0 || dur_ms <= 0) { usleep(dur_ms * 1000); return; }
    long long period = (long long)(1e9 / (double)freq);
    long long hi = period * g_duty / 100; if (hi < 1) hi = 1;
    long long lo = period - hi;           if (lo < 1) lo = 1;
    struct gpiohandle_data d;
    long long tick = now_ns();
    long long end = tick + (long long)dur_ms * 1000000LL;
    while (tick < end) {
        memset(&d, 0, sizeof(d));
        d.values[0] = 1;
        ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d);
        tick += hi;
        while (now_ns() < tick) ;
        if (tick >= end) break;
        memset(&d, 0, sizeof(d));
        d.values[0] = 0;
        ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d);
        tick += lo;
        while (now_ns() < tick) ;
    }
    memset(&d, 0, sizeof(d));
    d.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d);
}

static void gap(int ms) {
    usleep(ms * 1000);
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
    strncpy(req.consumer_label, "buzzer_esc", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL"); close(fd); return -1;
    }
    close(fd);
    return req.fd;
}

int main(int argc, char **argv) {
    int line = 23, only_esc1 = 0;
    int ai = 1;
    if (argc > 1 && !strcmp(argv[1], "esc1")) { only_esc1 = 1; ai = 2; }
    if (argc > ai) g_duty = atoi(argv[ai]);
    if (argc > ai + 1) line = atoi(argv[ai + 1]);
    if (g_duty < 2) g_duty = 2;
    if (g_duty > 50) g_duty = 50;
    if (line < 0 || line > 31) line = 23;

    int lfd = open_line(line);
    if (lfd < 0) return 1;

    int c6 = 84, d6 = 86, g6 = 91;          /* midi: C6 D6 G6 */
    int q = 260, h = 520, s = 900;          /* 四分/二分/段间 ms */

    printf("buzzer_esc duty=%d%% line=%d %s\n", g_duty, line,
           only_esc1 ? "(ESC1 only)" : "(ESC1-8)");

    if (only_esc1) {
        /* ESC1: C6 D6 G6 D6 G6，循环播放 */
        printf("ESC1: C6 D6 G6 D6 G6 (loop)\n");
        for (;;) {
            tone(lfd, midi_to_freq(c6), q); gap(20);
            tone(lfd, midi_to_freq(d6), q); gap(20);
            tone(lfd, midi_to_freq(g6), h); gap(20);
            tone(lfd, midi_to_freq(d6), q); gap(20);
            tone(lfd, midi_to_freq(g6), h);
            gap(s);
        }
    }

    /* ESC1: C6 D6 G6 D6 G6 */
    printf("ESC1: C6 D6 G6 D6 G6\n");
    tone(lfd, midi_to_freq(c6), q); gap(20);
    tone(lfd, midi_to_freq(d6), q); gap(20);
    tone(lfd, midi_to_freq(g6), h); gap(20);
    tone(lfd, midi_to_freq(d6), q); gap(20);
    tone(lfd, midi_to_freq(g6), h);
    gap(s);

    /* ESC2..ESC8: C6 D6 G6 */
    for (int esc = 2; esc <= 8; esc++) {
        printf("ESC%d: C6 D6 G6\n", esc);
        tone(lfd, midi_to_freq(c6), q); gap(20);
        tone(lfd, midi_to_freq(d6), q); gap(20);
        tone(lfd, midi_to_freq(g6), h);
        if (esc < 8) gap(s);
    }

    close(lfd);
    printf("done\n");
    return 0;
}
