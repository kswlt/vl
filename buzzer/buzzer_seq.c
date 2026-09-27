/*
 * buzzer_seq.c — 通用音符序列播放器（声音传真 / 任意 freq dur_ms 序列）
 * 用法: buzzer_seq <seq_file> [duty%] [line]
 * 序列文件每行: "<freq> <dur_ms>"，freq=0 表示静音
 * GPIO: 默认 line 23 (GPIO0_C7)，50% 占空比方波，忙等翻转
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <time.h>
#include <errno.h>

static int g_duty = 50;

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void msleep(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void tone_start(int lfd, int freq, long long *tick, long long *hi_ns, long long *lo_ns) {
    long long period_ns = (long long)(1e9 / (double)freq);
    *hi_ns = period_ns * g_duty / 100;
    if (*hi_ns < 1) *hi_ns = 1;
    *lo_ns = period_ns - *hi_ns;
    if (*lo_ns < 1) *lo_ns = 1;
    *tick = now_ns();
}

static void tone_next_cycle(int lfd, int high, long long *tick, long long hi_ns, long long lo_ns) {
    struct gpiohandle_data d;
    memset(&d, 0, sizeof(d));
    d.values[0] = high ? 1 : 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d);
    *tick += high ? hi_ns : lo_ns;
    while (now_ns() < *tick) ;
}

static void play_freq(int lfd, int freq, int dur_ms) {
    if (freq <= 0 || dur_ms <= 0) { msleep(dur_ms > 0 ? dur_ms : 0); return; }
    long long tick, hi_ns, lo_ns;
    tone_start(lfd, freq, &tick, &hi_ns, &lo_ns);
    long long end = tick + (long long)dur_ms * 1000000LL;
    while (tick < end) {
        tone_next_cycle(lfd, 1, &tick, hi_ns, lo_ns);
        if (tick >= end) break;
        tone_next_cycle(lfd, 0, &tick, hi_ns, lo_ns);
    }
    /* 结束拉到低电平 */
    struct gpiohandle_data d;
    memset(&d, 0, sizeof(d));
    d.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <seq_file> [duty%%] [line]\n", argv[0]);
        return 1;
    }
    const char *path = argv[1];
    if (argc > 2) g_duty = atoi(argv[2]);
    if (g_duty < 1) g_duty = 1;
    if (g_duty > 99) g_duty = 99;
    int line = argc > 3 ? atoi(argv[3]) : 23;

    FILE *fp = fopen(path, "r");
    if (!fp) { perror("open seq"); return 1; }

    /* 收集音符到内存（最多 200k 个） */
    int cap = 200000, n = 0;
    int *freqs = malloc(sizeof(int) * cap);
    int *durs  = malloc(sizeof(int) * cap);
    if (!freqs || !durs) { perror("malloc"); return 1; }
    int f, d;
    while (fscanf(fp, "%d %d", &f, &d) == 2 && n < cap) {
        freqs[n] = f; durs[n] = d; n++;
    }
    fclose(fp);
    if (n == 0) { fprintf(stderr, "empty seq\n"); return 1; }

    int lfd = open("/dev/gpiochip0", O_RDONLY);
    if (lfd < 0) { perror("open gpiochip"); return 1; }
    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = line;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    strcpy(req.consumer_label, "buzzer_seq");
    req.lines = 1;
    if (ioctl(lfd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE");
        return 1;
    }
    int hfd = req.fd;

    fprintf(stderr, "seq=%d notes duty=%d%% line=%d\n", n, g_duty, line);
    for (int i = 0; i < n; i++) {
        play_freq(hfd, freqs[i], durs[i]);
    }
    /* 收尾：保持低电平 */
    struct gpiohandle_data d0;
    memset(&d0, 0, sizeof(d0));
    d0.values[0] = 0;
    ioctl(hfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d0);
    close(hfd);
    close(lfd);
    free(freqs); free(durs);
    fprintf(stderr, "done\n");
    return 0;
}
