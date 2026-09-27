/*
 * buzzer_alarm.c — bomb countdown alarm for passive buzzer on RK3562.
 * 3100 Hz (measured resonant point) square wave, beep pattern speeds up
 * and lengthens until continuous blast — like a bomb countdown.
 * Usage: buzzer_alarm [line] [freq] [run_ms]   (default 23 / 3100 / run once)
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

/* drive a square wave for on_ms, then silence for off_ms (0 = off phase skip) */
static void beep(int lfd, int freq, long on_ms, long off_ms, long long half_ns) {
    long long t0 = now_ns();
    long long t = t0;
    int v = 0;
    while (t - t0 < on_ms * 1000000LL) {
        v ^= 1;
        struct gpiohandle_data data;
        memset(&data, 0, sizeof(data));
        data.values[0] = v;
        ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
        t += half_ns;
        sleep_abs(t);
    }
    /* silence */
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    if (off_ms > 0) msleep(off_ms);
}

int main(int argc, char** argv) {
    int line = 23;
    int freq = 3100;
    if (argc > 1) line = atoi(argv[1]);
    if (argc > 2) freq = atoi(argv[2]);
    if (line < 0 || line > 31) line = 23;
    if (freq < 100 || freq > 10000) freq = 3100;

    long long half_ns = (long long)(1e9 / (2.0 * freq));

    int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) { perror("open gpiochip0"); return 1; }

    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = line;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.lines = 1;
    strncpy(req.consumer_label, "alarm", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL");
        close(fd);
        return 1;
    }
    close(fd);
    int lfd = req.fd;
    printf("alarm line=%d freq=%d\n", line, freq);
    fflush(stdout);

    /* countdown pattern: beep gets faster and longer until continuous blast */
    struct { long on_ms, off_ms; int repeat; } ph[] = {
        { 180, 820,  3 },   /* slow tick  x3 */
        { 250, 550,  4 },   /* normal beep x4 */
        { 320, 280,  4 },   /* quicker x4 */
        { 250, 150,  5 },   /* rapid x5 */
        { 120, 100,  6 },   /* urgent x6 */
        { 800, 120,  2 },   /* long blast, short gap */
        { 3000, 0,   1 },   /* final continuous blast */
    };
    int n_ph = sizeof(ph) / sizeof(ph[0]);
    for (int i = 0; i < n_ph; i++) {
        for (int r = 0; r < ph[i].repeat; r++) {
            printf("phase%d on=%ld off=%ld\n", i, ph[i].on_ms, ph[i].off_ms);
            fflush(stdout);
            beep(lfd, freq, ph[i].on_ms, ph[i].off_ms, half_ns);
        }
    }

    /* low at end */
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    close(lfd);
    printf("alarm done\n");
    return 0;
}
