/*
 * buzzer_zhenai.c — 《真爱假说》(庄东茹) melody on the passive buzzer.
 * Key: Db major, simplified to root single-line. 20% duty (quiet).
 * Sheet: 553555355362 | 553555355351 | 66563 | 333233352 | 2122 212122231
 * Usage: buzzer_zhenai [line]
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

/* Db major, octave 5: 1=Db5(554) 2=Eb5(622) 3=F5(698) 5=Ab5(830) 6=Bb5(932) */
#define D1 554
#define D2 622
#define D3 698
#define D5 830
#define D6 932
#define N 440   /* eighth-note ~0.44s */
#define BR 360   /* breath between phrases */

static const struct note melody[] = {
    /* 5 5 3 5 5 5 5 3 5 3 6 2 */
    {D5,N},{D5,N},{D3,N},{D5,N},{D5,N},{D5,N},{D5,N},{D3,N},{D5,N},{D3,N},{D6,N},{D2,N},
    {0,BR},
    /* 5 5 3 5 5 5 5 3 5 5 3 5 1 */
    {D5,N},{D5,N},{D3,N},{D5,N},{D5,N},{D5,N},{D5,N},{D3,N},{D5,N},{D5,N},{D3,N},{D5,N},{D1,N},
    {0,BR},
    /* 6 6 5 6 3 */
    {D6,N},{D6,N},{D5,N},{D6,N},{D3,N},
    {0,BR},
    /* 3 3 3 2 3 3 3 5 2 */
    {D3,N},{D3,N},{D3,N},{D2,N},{D3,N},{D3,N},{D3,N},{D5,N},{D2,N},
    {0,BR},
    /* 2 1 2 2 2 1 2 1 2 2 2 3 1 */
    {D2,N},{D1,N},{D2,N},{D2,N},{D2,N},{D1,N},{D2,N},{D1,N},{D2,N},{D2,N},{D2,N},{D3,N},{D1,N},
    {0,BR*2},
};

#define DUTY_PERCENT 20

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
    msleep(12);
}

int main(int argc, char** argv) {
    int line = 23;
    if (argc > 1) line = atoi(argv[1]);
    if (line < 0 || line > 31) line = 23;

    int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) { perror("open gpiochip0"); return 1; }
    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = line;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.lines = 1;
    strncpy(req.consumer_label, "zhenai", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL"); close(fd); return 1;
    }
    close(fd);
    int lfd = req.fd;
    printf("zhen ai jia shuo line=%d\n", line);
    fflush(stdout);

    int n = sizeof(melody) / sizeof(melody[0]);
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < n; i++) play_note(lfd, melody[i].freq, melody[i].ms);
        msleep(400);
    }

    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    close(lfd);
    printf("done\n");
    return 0;
}
