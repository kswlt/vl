/*
 * buzzer_song.c — play "东南苦行山" (殷正洋) on the passive buzzer.
 * C-major notation converted to square-wave tones on GPIO0_C7 (line 23).
 * Usage: buzzer_song [line] [tempo_ms]   (tempo = ms per beat, default 380)
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

struct note { int freq; double beats; };

/* 东南苦行山 — C major. , = low octave, ' = high octave. */
static const struct note melody[] = {
    /* 主歌A: 来自中原一群伙伴,结庐东南山 */
    {220,1},{330,1},{294,0.5},{392,0.5},{330,0.5},{294,0.5},
    {330,1},{330,1},{330,0.5},{294,0.5},{294,2},
    {262,1},{220,1},{196,1},{165,0.5},{196,0.5},
    {220,4},
    {262,1},{220,1},{294,0.5},{262,0.5},{294,0.5},{330,0.5},
    {392,1},{330,0.5},{294,0.5},{330,1},{330,0.5},{330,0.5},
    {294,1},{262,1},{220,1},{196,1},
    {220,4},
    /* 主歌A': 秉承千年卓绝意志,潜修东南山 */
    {220,1},{330,1},{294,0.5},{392,0.5},{330,0.5},{294,0.5},
    {330,1},{330,1},{330,0.5},{294,0.5},{294,2},
    {262,1},{220,1},{196,1},{165,0.5},{196,0.5},
    {220,4},
    {262,1},{220,1},{294,0.5},{262,0.5},{294,0.5},{330,0.5},
    {392,1},{330,0.5},{294,0.5},{330,1},{330,0.5},{330,0.5},
    {294,1},{262,1},{220,1},{196,1},
    {220,3},
    /* 副歌B1: 香火在雨中烧... */
    {330,0.5},{392,0.5},
    {440,1},{440,0.5},{440,0.5},{392,1},{330,0.5},{294,0.5},
    {330,1},{392,0.5},{330,0.5},{294,2},
    {330,1},{330,1},{294,0.5},{330,0.5},{294,0.5},{262,0.5},
    {220,1},{247,0.5},{196,0.5},{220,1.5},{262,0.5},
    {294,1},{220,1},{262,0.5},{294,0.5},{294,0.5},{330,0.5},
    {392,1},{392,1},{392,1},{330,0.5},{392,0.5},
    {440,1},{523,1},{494,0.5},{440,0.5},{392,1},
    {440,1},{440,1},{440,1},{330,0.5},{392,0.5},
    /* 副歌B2 */
    {440,1},{440,0.5},{440,0.5},{392,1},{330,0.5},{294,0.5},
    {330,1},{392,0.5},{330,0.5},{294,2},
    {330,1},{330,1},{294,0.5},{330,0.5},{294,0.5},{262,0.5},
    {220,1},{247,0.5},{196,0.5},{220,1.5},{262,0.5},
    {294,1},{220,1},{262,0.5},{294,0.5},{294,0.5},{330,0.5},
    {392,1},{392,1},{392,1},{330,0.5},{392,0.5},
    {440,1},{523,1},{494,0.5},{440,0.5},{392,1},
    {440,4},
};

static void play_note(int lfd, int freq, long dur_ms) {
    long long half_ns = (long long)(1e9 / (2.0 * freq));
    long long t0 = now_ns();
    long long t = t0;
    int v = 0;
    while (t - t0 < dur_ms * 1000000LL) {
        v ^= 1;
        struct gpiohandle_data data;
        memset(&data, 0, sizeof(data));
        data.values[0] = v;
        ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
        t += half_ns;
        sleep_abs(t);
    }
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    msleep(25);   /* note gap */
}

int main(int argc, char** argv) {
    int line = 23;
    int tempo = 380;
    if (argc > 1) line = atoi(argv[1]);
    if (argc > 2) tempo = atoi(argv[2]);
    if (line < 0 || line > 31) line = 23;
    if (tempo < 120 || tempo > 1200) tempo = 380;

    int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) { perror("open gpiochip0"); return 1; }

    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = line;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.lines = 1;
    strncpy(req.consumer_label, "song", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL");
        close(fd);
        return 1;
    }
    close(fd);
    int lfd = req.fd;
    printf("song=东南苦行山 line=%d tempo=%d\n", line, tempo);
    fflush(stdout);

    int n = sizeof(melody) / sizeof(melody[0]);
    for (int i = 0; i < n; i++) {
        long dur = (long)(melody[i].beats * tempo);
        play_note(lfd, melody[i].freq, dur);
    }
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    close(lfd);
    printf("song done\n");
    return 0;
}
