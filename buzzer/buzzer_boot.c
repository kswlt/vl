/*
 * buzzer_boot.c — 10 s boot-complete chime for the passive buzzer.
 * Designed melody: ascending arpeggio (G5-C6-E6-G6) = "all systems up",
 * gentle descent, then a long closing note.
 * Usage: buzzer_boot [line]
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

/* ~9 s industrial boot chime: frequency sweep-up (startup feel, sound
 * grows from soft to loud as freq approaches the 3300 Hz resonance),
 * then three confirm beeps and a final long blast. freq 0 = silence. */
static const struct note melody[] = {
    {0,    600},   /* lead-in */
    {1000, 600},   /* sweep start, quiet */
    {1500, 600},   /* rising */
    {2000, 600},   /* rising */
    {2500, 600},   /* rising */
    {3000, 600},   /* nearly at resonance */
    {3300, 700},   /* rated / loudest */
    {0,    400},   /* pause */
    {3300, 200},   /* confirm beep 1 */
    {0,    200},
    {3300, 200},   /* confirm beep 2 */
    {0,    200},
    {3300, 200},   /* confirm beep 3 */
    {0,    500},
    {3300, 1800},  /* final long blast: system up */
    {0,    800},   /* tail */
};

static void play_note(int lfd, int freq, long dur_ms) {
    if (freq <= 0) { msleep(dur_ms); return; }
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
    strncpy(req.consumer_label, "bootbeep", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL");
        close(fd);
        return 1;
    }
    close(fd);
    int lfd = req.fd;
    printf("boot chime line=%d\n", line);
    fflush(stdout);

    int n = sizeof(melody) / sizeof(melody[0]);
    for (int i = 0; i < n; i++) {
        play_note(lfd, melody[i].freq, melody[i].ms);
    }
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    close(lfd);
    printf("boot chime done\n");
    return 0;
}
