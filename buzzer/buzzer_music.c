/*
 * buzzer_music.c — play a melody on the passive buzzer (RK3562, GPIO0_C7=line 23).
 * Square-wave tones per note, 30 ms gap between notes.
 * Default: "Twinkle Twinkle Little Star" in C major (C4-C5).
 * Usage: buzzer_music [line] [tempo] [loop]   (tempo ms/beat, default 400)
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

struct note { int freq; int beats; };

/* Twinkle Twinkle Little Star, 4/4 */
static const struct note melody[] = {
    {262,2},{262,2},{392,2},{392,2},{440,2},{440,2},{392,4},
    {349,2},{349,2},{330,2},{330,2},{294,2},{294,2},{262,4},
    {392,2},{392,2},{349,2},{349,2},{330,2},{330,2},{294,4},
    {392,2},{392,2},{349,2},{349,2},{330,2},{330,2},{294,4},
    {262,2},{262,2},{392,2},{392,2},{440,2},{440,2},{392,4},
    {349,2},{349,2},{330,2},{330,2},{294,2},{294,2},{262,4},
};

/* play one note: square wave at freq for dur_ms, then 30 ms gap */
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
    msleep(30);   /* note gap */
}

int main(int argc, char** argv) {
    int line = 23;
    int tempo = 400;   /* ms per beat */
    int loops = 1;
    if (argc > 1) line = atoi(argv[1]);
    if (argc > 2) tempo = atoi(argv[2]);
    if (argc > 3) loops = atoi(argv[3]);
    if (line < 0 || line > 31) line = 23;
    if (tempo < 100 || tempo > 2000) tempo = 400;
    if (loops < 1 || loops > 20) loops = 1;

    int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) { perror("open gpiochip0"); return 1; }

    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = line;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.lines = 1;
    strncpy(req.consumer_label, "music", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL");
        close(fd);
        return 1;
    }
    close(fd);
    int lfd = req.fd;
    printf("music line=%d tempo=%d loops=%d\n", line, tempo, loops);
    fflush(stdout);

    int n = sizeof(melody) / sizeof(melody[0]);
    for (int L = 0; L < loops; L++) {
        for (int i = 0; i < n; i++) {
            printf("note %3d: %d Hz x%d\n", i, melody[i].freq, melody[i].beats);
            play_note(lfd, melody[i].freq, melody[i].beats * tempo);
        }
    }
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    close(lfd);
    printf("music done\n");
    return 0;
}
