/*
 * buzzer_mowen.c — 《莫问归期》(蒋雪儿) verse melody on the passive buzzer.
 * Key: E major (1=E4), 4/4, eighth-note ~0.42s (♩=72), 20% duty.
 * Usage: buzzer_mowen [line]
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

/* E major: 1=E4(330) 2=F#4(370) 3=G#4(415) 5=B4(494) 6=C#5(554) 7=D#5(622) */
#define E1 330
#define E2 370
#define E3 415
#define E5 494
#define E6 554
#define E7 622
#define N 420   /* eighth note at ♩=72 */
#define H 840   /* quarter */
#define P 300   /* phrase breath */

static const struct note melody[] = {
    /* line 1: 6 6 5 6 3 3 | 7 7 7 7 6 5 3 | 6 6 5 6 3 3 5 | 6 5 3 3 */
    {E6,N},{E6,N},{E5,N},{E6,N},{E3,N},{E3,N},
    {E7,N},{E7,N},{E7,N},{E7,N},{E6,N},{E5,N},{E3,N},
    {E6,N},{E6,N},{E5,N},{E6,N},{E3,N},{E3,N},{E5,N},
    {E6,N},{E5,N},{E3,N},{E3,H},
    {0,P},
    /* line 2: 6 6 5 6 3 3 | 6 6 5 6 1 6 6 | 6 1 6 5 6 1 1 6 | 2 3 2 2 5 6 1 */
    {E6,N},{E6,N},{E5,N},{E6,N},{E3,N},{E3,N},
    {E6,N},{E6,N},{E5,N},{E6,N},{E1,N},{E6,N},{E6,N},
    {E6,N},{E1,N},{E6,N},{E5,N},{E6,N},{E1,N},{E1,N},{E6,N},
    {E2,N},{E3,N},{E2,N},{E2,N},{E5,N},{E6,N},{E1,H},
    {0,P*2},
};

static int DUTY_PERCENT = 8;

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
    strncpy(req.consumer_label, "mowen", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL"); close(fd); return 1;
    }
    close(fd);
    int lfd = req.fd;
    printf("mo wen gui qi line=%d duty=%d%%\n", line, DUTY_PERCENT);
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
