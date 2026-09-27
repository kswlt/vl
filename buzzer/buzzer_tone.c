/*
 * buzzer_tone.c — passive buzzer driver for RK3562 via GPIO character device
 * GPIO0_C6 = gpiochip0 line 22 (usergpio, output, 12mA)
 * Generates a square wave at given frequency using absolute-time clock.
 * Usage: buzzer_tone [freq_hz] [duration_s] [line]
 *   buzzer_tone bench   -> run ioctl throughput benchmark only
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

int main(int argc, char** argv) {
    int freq = 2700;       /* Hz, passive buzzer resonance typically 2.7-4k */
    double dur = 3.0;      /* seconds */
    int line = 22;         /* GPIO0_C6 by default */
    int bench_only = 0;

    if (argc > 1 && strcmp(argv[1], "bench") == 0) {
        bench_only = 1;
    } else {
        if (argc > 1) freq = atoi(argv[1]);
        if (argc > 2) dur = atof(argv[2]);
        if (argc > 3) line = atoi(argv[3]);
        if (freq < 10 || freq > 20000) { fprintf(stderr, "freq out of range\n"); return 1; }
        if (dur < 0.05 || dur > 120)    { fprintf(stderr, "dur out of range\n"); return 1; }
        if (line < 0 || line > 31)      { fprintf(stderr, "line out of range\n"); return 1; }
    }

    int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) { perror("open /dev/gpiochip0"); return 1; }

    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = line;               /* default 22 = GPIO0_C6 */
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.lines = 1;
    strncpy(req.consumer_label, "buzzer", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL");
        close(fd);
        return 1;
    }
    close(fd);
    int lfd = req.fd;
    printf("line=%d freq=%d dur=%.1fs\n", line, freq, dur);

    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));

    /* --- benchmark: max toggle rate --- */
    const int N = 4000;
    long long t0 = now_ns();
    for (int i = 0; i < N; i++) {
        data.values[0] = i & 1;
        if (ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) < 0) {
            perror("SET_LINE_VALUES (bench)");
            return 1;
        }
    }
    long long t1 = now_ns();
    double sec = (double)(t1 - t0) / 1e9;
    printf("bench: %d toggles in %.3f s -> %.0f toggles/s (square-wave ceiling %.0f Hz)\n",
           N, sec, N / sec, N / sec / 2.0);

    if (bench_only) {
        data.values[0] = 0;
        ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
        close(lfd);
        return 0;
    }

    /* --- tone generation --- */
    long long half_ns = (long long)(1e9 / (2.0 * freq));
    long long total_ns = (long long)(dur * 1e9);
    long long start = now_ns();
    long long t = start;
    int v = 0;
    while (t - start < total_ns) {
        v ^= 1;
        data.values[0] = v;
        ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
        t += half_ns;
        sleep_abs(t);
    }
    data.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    close(lfd);
    printf("tone done: %d Hz for %.1f s\n", freq, dur);
    return 0;
}
