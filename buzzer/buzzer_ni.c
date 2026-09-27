/*
 * buzzer_ni.c — play GALA《你》(《机甲大师》片尾曲) on the passive buzzer.
 * 1=G, 4/4, ~80 BPM. Square-wave tones on GPIO0_C7 (line 23).
 * Usage: buzzer_ni [line] [tempo_ms]   (tempo = ms per beat, default 750)
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

struct note { int freq; double beats; };   /* freq 0 = rest */

/* G major: 1=392 2=440 3=494 4=523 5=587 6=659 7=740 ; 5,=294 7,=370 ; 1'=784 */
static const struct note melody[] = {
    /* 主歌1: 你从天而降的你,落在我的马背上 */
    {294,1.5},{392,0.5},{440,1},{494,0.5},{587,0.5},
    {587,1},{494,0.5},{587,0.5},{392,2},
    {440,0.5},{440,0.5},{440,0.5},{494,0.5},{440,0.5},{587,0.5},{392,0.5},{370,0.5},
    {587,1},{587,1},{587,1},{0,1},
    /* 主歌1b: 如玉的模样... */
    {294,0.5},{294,1},{392,0.5},{440,1},{494,0.5},{587,0.5},
    {587,0.5},{587,0.5},{587,0.5},{587,0.5},{494,0.5},{587,0.5},{392,1},
    {440,0.5},{440,0.5},{440,0.5},{494,0.5},{440,0.5},{587,0.5},{392,0.5},{370,0.5},
    {370,0.5},{294,0.5},{294,3},
    /* 副歌: 你在那万人中央,感受那万丈荣光 */
    {587,1.5},{587,0.5},{587,1},{587,0.5},{784,0.5},
    {587,1.5},{587,0.5},{587,1},{494,0.5},{494,0.5},
    {440,1.5},{392,0.5},{440,0.5},{494,0.5},{587,0.5},{440,0.5},
    {494,1},{494,1},{494,1},{0,1},
    {587,1.5},{392,0.5},{440,1},{494,0.5},{587,0.5},
    {587,1},{587,1},{494,0.5},{587,0.5},{392,1},
    {440,0.5},{440,1},{494,0.5},{440,0.5},{587,0.5},{392,0.5},{370,0.5},
    {587,1},{587,1},{587,1},{0,1},
    /* 主歌1b 反复 */
    {294,0.5},{294,1},{392,0.5},{440,1},{494,0.5},{587,0.5},
    {587,0.5},{587,0.5},{587,0.5},{587,0.5},{494,0.5},{587,0.5},{392,1},
    {440,0.5},{440,0.5},{440,0.5},{494,0.5},{440,0.5},{587,0.5},{392,0.5},{370,0.5},
    {370,0.5},{294,0.5},{294,3},
    /* 副歌 第二遍 */
    {587,1.5},{587,0.5},{587,1},{587,0.5},{784,0.5},
    {587,1.5},{587,0.5},{587,1},{494,0.5},{494,0.5},
    {440,1.5},{392,0.5},{440,0.5},{494,0.5},{587,0.5},{440,0.5},
    {494,1},{494,1},{494,1},{0,1},
    {587,1.5},{392,0.5},{440,1},{494,0.5},{587,0.5},
    {587,1},{587,1},{494,0.5},{587,0.5},{392,1},
    /* 尾句: 2x2x2x3x5x5x1gx7xx7xx|(7x5x)(5--|5)--0 (简化) */
    {440,0.5},{440,0.5},{440,0.5},{494,0.5},{587,0.5},{587,0.5},{784,0.5},{740,0.5},{740,0.5},
    {370,0.5},{294,0.5},{294,2},{0,1},
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
    msleep(25);
}

int main(int argc, char** argv) {
    int line = 23;
    int tempo = 750;
    if (argc > 1) line = atoi(argv[1]);
    if (argc > 2) tempo = atoi(argv[2]);
    if (line < 0 || line > 31) line = 23;
    if (tempo < 200 || tempo > 2000) tempo = 750;

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
    printf("song=你(机甲大师) line=%d tempo=%d\n", line, tempo);
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
