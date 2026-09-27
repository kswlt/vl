/*
 * buzzer_djiseg.c — play back the DJI boot-recording segments 1/2/4
 * transcribed from the user's screen recording.
 *   seg1: A6 -> G5 -> F#5 descending three-note prompt
 *   seg2: full note-faithful 8-bit melody (5.5-8.6s)
 *   seg4: C#5 -> D6 -> G5 chime ending
 * Usage: buzzer_djiseg <1|2|4> [line]
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

/* seg1: A6(1760) -> G5(780) -> F#5(760) */
static const struct note seg1[] = {
    {1760, 180}, {780, 330}, {760, 300},
};
/* seg4: C#5 -> D6 -> G5 chime */
static const struct note seg4[] = {
    {540, 200}, {1180, 225}, {800, 275}, {1180, 100},
};
/* seg2: faithful note sequence from the recording */
static const struct note seg2[] = {
    {516, 90}, {400, 150}, {466, 120}, {466, 120}, {466, 120},
    {450, 120}, {400, 90}, {450, 30}, {566, 30}, {400, 60},
    {466, 30}, {400, 120}, {566, 30}, {683, 30}, {600, 120},
    {566, 30}, {566, 30}, {766, 30}, {566, 90}, {683, 90},
    {616, 60}, {566, 30}, {600, 60}, {566, 30}, {600, 30},
    {583, 30}, {583, 120}, {600, 60}, {766, 30}, {400, 90},
    {616, 30}, {450, 60}, {766, 30},
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
    msleep(12);
}

int main(int argc, char** argv) {
    int seg = 2;
    if (argc > 1) seg = atoi(argv[1]);
    int line = 23;
    if (argc > 2) line = atoi(argv[2]);
    if (line < 0 || line > 31) line = 23;

    int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) { perror("open gpiochip0"); return 1; }
    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = line;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.lines = 1;
    strncpy(req.consumer_label, "djiseg", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL"); close(fd); return 1;
    }
    close(fd);
    int lfd = req.fd;
    printf("dji segment %d line=%d\n", seg, line);
    fflush(stdout);

    const struct note *m; int n;
    if (seg == 1) { m = seg1; n = sizeof(seg1)/sizeof(seg1[0]); }
    else if (seg == 4) { m = seg4; n = sizeof(seg4)/sizeof(seg4[0]); }
    else { m = seg2; n = sizeof(seg2)/sizeof(seg2[0]); }
    for (int i = 0; i < n; i++) play_note(lfd, m[i].freq, m[i].ms);
    if (seg == 2) for (int r = 0; r < 2; r++) {
        for (int i = 0; i < n; i++) play_note(lfd, m[i].freq, m[i].ms);
    }

    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    close(lfd);
    printf("done\n");
    return 0;
}
