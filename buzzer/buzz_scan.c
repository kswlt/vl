/*
 * buzz_scan.c — sweep GPIO0 free lines one by one, driving each with a
 * 2700 Hz square wave for a hold time, so the physical header pin feeding
 * the passive buzzer can be identified by ear.
 * Usage: buzz_scan [hold_ms] [freq_hz]   (default 2000 ms, 2700 Hz)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <time.h>
#include <errno.h>

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

int main(int argc, char** argv) {
    long hold_ms = 2000;
    int freq = 2700;
    if (argc > 1) hold_ms = atol(argv[1]);
    if (argc > 2) freq = atoi(argv[2]);
    if (hold_ms < 500 || hold_ms > 10000) hold_ms = 2000;
    if (freq < 100 || freq > 10000) freq = 2700;

    long long half_ns = (long long)(1e9 / (2.0 * freq));

    int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) { perror("open gpiochip0"); return 1; }

    for (int line = 0; line < 32; line++) {
        struct gpiohandle_request req;
        memset(&req, 0, sizeof(req));
        req.lineoffsets[0] = line;
        req.flags = GPIOHANDLE_REQUEST_OUTPUT;
        req.default_values[0] = 0;
        req.lines = 1;
        snprintf(req.consumer_label, sizeof(req.consumer_label), "buzzscan");
        if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
            printf("line %2d: BUSY (%s)\n", line, strerror(errno));
            fflush(stdout);
            continue;
        }
        int lfd = req.fd;
        printf("line %2d: %d Hz for %ld ms\n", line, freq, hold_ms);
        fflush(stdout);

        long long t0 = now_ns();
        long long t = t0;
        int v = 0;
        while (t - t0 < hold_ms * 1000000LL) {
            v ^= 1;
            struct gpiohandle_data data;
            memset(&data, 0, sizeof(data));
            data.values[0] = v;
            ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
            t += half_ns;
            sleep_abs(t);
        }
        /* release to low */
        struct gpiohandle_data data;
        memset(&data, 0, sizeof(data));
        data.values[0] = 0;
        ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
        close(lfd);
    }
    close(fd);
    printf("scan done\n");
    return 0;
}
