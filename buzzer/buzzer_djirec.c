/*
 * buzzer_djirec.c — buzzer transcription of the DJI boot music from the
 * user's screen recording (seg 5.5-8.6s): G4/A#4 pulse pattern + rising
 * D5/E5 melody + long-note ending.
 * Usage: buzzer_djirec [line]
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

/* G4=392 A4=440 A#4=466 B4=494 D5=587 D#5=622 E5=659 G5=784 */
static const struct note melody[] = {
    {392, 160},   /* G4  */
    {466, 100},   /* A#4 */
    {392, 220},   /* G4  */
    {440, 120},   /* A4  */
    {392, 100},   /* G4  */
    {587, 60},    /* D5  */
    {392, 260},   /* G4  hold */
    {659, 90},    /* E5  */
    {587, 260},   /* D5  hold */
    {659, 90},    /* E5  */
    {587, 260},   /* D5  hold */
    {587, 220},   /* D5  */
    {587, 140},   /* D5  */
    {392, 120},   /* G4  tail */
    {784, 220},   /* G5  final */
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
    msleep(10);   /* gap */
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
    strncpy(req.consumer_label, "djirec", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL");
        close(fd);
        return 1;
    }
    close(fd);
    int lfd = req.fd;
    printf("dji rec music line=%d\n", line);
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
    printf("done\n");
    return 0;
}
