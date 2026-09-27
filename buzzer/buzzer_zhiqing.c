/*
 * buzzer_zhiqing.c — 《纸短情长》(花粥) chorus on passive buzzer.
 * Note sequence from the Sky-co-single-note chart (per-syllable singing).
 * Key C (5.=G4 ... 1'=C6). Usage: buzzer_zhiqing [duty] [line]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <time.h>

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
static void sleep_abs(long long t_ns) {
    struct timespec ts;
    ts.tv_sec = t_ns / 1000000000LL;
    ts.tv_nsec = t_ns % 1000000000LL;
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}
static void msleep(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
struct note { int freq; int ms; };

/* C major around C5; . = lower octave, ' = upper octave */
#define L5 392    /* 5. G4 */
#define L6 440    /* 6. A4 */
#define L7 494    /* 7. B4 */
#define C1 523
#define C2 587
#define C3 659
#define C4 698
#define C5 784
#define C6 880
#define C7 988
#define H1 1047   /* 1' C6 */
#define N 380     /* eighth @ ~79bpm */
#define Q 760     /* quarter */
#define HN 190    /* sixteenth-ish quick notes */
#define P 220     /* phrase breath */

static const struct note melody[] = {
    /* 纸短情长啊 */
    {C5,N},{C6,N},{H1,N},{C5,HN},{C6,HN},{C6,Q},
    /* 道不尽太多涟漪 */
    {C6,N},{C5,HN},{C4,HN},{C5,N},{C3,N},{C2,N},{C1,Q},
    {0,P},
    /* 我的故事都是关于你呀 */
    {L6,HN},{C1,N},{C3,N},{C2,N},{C2,HN},{C1,N},{C2,N},{C5,N},{C1,N},{C2,Q},
    {0,P},
    /* 怎么会爱上了她 */
    {C1,N},{C1,HN},{C2,N},{C3,HN},{C3,HN},{C2,N},{C3,Q},
    /* 并决定跟她回家 */
    {C3,N},{C2,HN},{C1,HN},{C5,N},{C5,HN},{C3,N},{C5,Q},
    {0,P},
    /* 放弃了我的所有 */
    {C5,N},{C6,HN},{C7,HN},{H1,N},{H1,N},{H1,N},{H1,Q},
    /* 我的一切无所谓 */
    {H1,N},{C7,HN},{C6,HN},{C7,N},{C6,HN},{C3,N},{C5,Q},
    {0,P},
    /* 纸短情长啊 */
    {C5,N},{C6,N},{H1,N},{C5,HN},{C6,HN},{C6,Q},
    /* 诉不完当时年少 */
    {C6,N},{C5,HN},{C4,HN},{C5,N},{C3,N},{C2,N},{C1,Q},
    {0,P},
    /* 我的故事还是关于你呀 */
    {L6,HN},{C1,N},{C3,N},{C2,HN},{C2,HN},{C1,N},{C2,N},{C1,N},{L6,N},{C1,Q},
    {0,P*2},
};

static int DUTY_PERCENT = 5;

static void play_note(int lfd, int freq, long dur_ms) {
    if (freq <= 0) { msleep(dur_ms); return; }
    long long period_ns = (long long)(1e9 / freq);
    long long hi_ns = period_ns * DUTY_PERCENT / 100;
    long long lo_ns = period_ns - hi_ns;
    long long t0 = now_ns();
    long long t = t0;
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    while (t - t0 < dur_ms * 1000000LL) {
        data.values[0] = 1;
        ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
        t += hi_ns;
        sleep_abs(t);
        data.values[0] = 0;
        ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
        t += lo_ns;
        sleep_abs(t);
    }
    data.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    msleep(10);
}

int main(int argc, char** argv) {
    int line = 23;
    if (argc > 1) DUTY_PERCENT = atoi(argv[1]);
    if (argc > 2) line = atoi(argv[2]);
    if (DUTY_PERCENT < 2) DUTY_PERCENT = 2;
    if (DUTY_PERCENT > 50) DUTY_PERCENT = 50;
    if (line < 0 || line > 31) line = 23;

    int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) { perror("open gpiochip0"); return 1; }
    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = line;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.lines = 1;
    strncpy(req.consumer_label, "zhiqing", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL"); close(fd); return 1;
    }
    close(fd);
    int lfd = req.fd;
    printf("zhi duan qing chang v3 line=%d duty=%d%%\n", line, DUTY_PERCENT);
    fflush(stdout);

    int n = sizeof(melody) / sizeof(melody[0]);
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < n; i++) play_note(lfd, melody[i].freq, melody[i].ms);
        msleep(500);
    }

    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    close(lfd);
    printf("done\n");
    return 0;
}
