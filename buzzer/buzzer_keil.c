/*
 * buzzer_keil.c — Keil keygen music style chiptune for the passive buzzer.
 * F major, ~187 BPM, square-wave arpeggio + melody (8-bit tracker vibe).
 * Recreated from the well-known character of "Keil Generic Keygen - EDGE"
 * music; not a note-exact transcription.
 * Usage: buzzer_keil [line]
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

/* F major notes: F4=349 G4=392 A4=440 Bb4=466 C5=523 D5=587 E5=659
 * F5=698 G5=784 A5=880 Bb5=932 C6=1047 D6=1175 E6=1319 */
#define F5 698
#define G5 784
#define A5 880
#define B5 932
#define C6 1047
#define D6 1175
#define E6 1319

/* e=8th note (~160ms @187bpm), q=quarter (~320ms), h=half (~640ms) */
#define E 160
#define Q 320
#define H 640

/* 16-bar loop: arpeggio + melody, then repeat */
static const struct note melody[] = {
    /* bar 1: F5 A5 C6 A5 | F5 A5 C6 D6 */
    {F5,E},{A5,E},{C6,E},{A5,E},{F5,E},{A5,E},{C6,E},{D6,E},
    /* bar 2: E6 C6 A5 C6 | D6 C6 A5 G5 */
    {E6,E},{C6,E},{A5,E},{C6,E},{D6,E},{C6,E},{A5,E},{G5,E},
    /* bar 3: same as bar 1 */
    {F5,E},{A5,E},{C6,E},{A5,E},{F5,E},{A5,E},{C6,E},{D6,E},
    /* bar 4: E6 D6 C6 A5 | C6 A5 F5(hold) */
    {E6,E},{D6,E},{C6,E},{A5,E},{C6,E},{A5,E},{F5,H},{F5,Q},
    /* bar 5: G5 A5 Bb5 A5 | G5 A5 C6 Bb5 */
    {G5,E},{A5,E},{B5,E},{A5,E},{G5,E},{A5,E},{C6,E},{B5,E},
    /* bar 6: A5 G5 F5 G5 | A5 Bb5 C6 D6 */
    {A5,E},{G5,E},{F5,E},{G5,E},{A5,E},{B5,E},{C6,E},{D6,E},
    /* bar 7: E6 D6 C6 D6 | E6 C6 A5 C6 */
    {E6,E},{D6,E},{C6,E},{D6,E},{E6,E},{C6,E},{A5,E},{C6,E},
    /* bar 8: D6 C6 A5 G5 | A5 G5 F5(hold) */
    {D6,E},{C6,E},{A5,E},{G5,E},{A5,E},{G5,E},{F5,H},{F5,Q},
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
    msleep(8);   /* tiny gap */
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
    strncpy(req.consumer_label, "keil", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL");
        close(fd);
        return 1;
    }
    close(fd);
    int lfd = req.fd;
    printf("keil chiptune line=%d\n", line);
    fflush(stdout);

    int n = sizeof(melody) / sizeof(melody[0]);
    for (int rep = 0; rep < 4; rep++) {   /* 4 repetitions = ~21s */
        for (int i = 0; i < n; i++) {
            play_note(lfd, melody[i].freq, melody[i].ms);
        }
        msleep(120);
    }
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    close(lfd);
    printf("keil chiptune done\n");
    return 0;
}
