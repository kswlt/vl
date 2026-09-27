/*
 * buzzer_play.c — 单音 8-bit 蜂鸣器音乐播放器 (RK3562 / Linux GPIO)
 *
 * 设计目标：复古游戏机 square-wave 单音旋律，不用 PCM、不和弦、不混音。
 *
 * 使用：
 *   buzzer_play test                播放测试音阶 C-E-G-C
 *   buzzer_play 520am               播放《5:20AM》riff
 *   buzzer_play 520am 50 23 1.0 0    duty line tempo transpose
 *
 * 参数：
 *   duty      PWM 占空比 %，默认 50（标准方波音色）；嫌吵可 20~35
 *   line      GPIO line，默认 23（GPIO0_C7，实测蜂鸣器所在）
 *   tempo     速度倍率，>1 加快，<1 减慢；默认 1.0
 *   transpose 移调半音数，默认 0；音域不适可整体移调
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <time.h>

/* ============================ 音高 ============================ */
/* A4=440 十二平均律；midi: 60=C4 ... 72=C5 ... 84=C6 ... 96=C7 */
static int midi_to_freq(int midi) {
    if (midi <= 0) return 0;
    return (int)lround(440.0 * pow(2.0, (double)(midi - 69) / 12.0));
}

/* ============================ 数据结构 ============================ */
struct Note {          /* 音符序列：标准结构 */
    uint16_t frequency;   /* Hz，0=休止 */
    uint16_t duration_ms; /* 时长 */
};

struct MNote {         /* 源序列：midi 号 + 时长（便于移调） */
    int midi;             /* midi 号，0=休止 */
    int dur_ms;
};

/* 音域钳制（默认 C4~C7；蜂鸣器频响考虑，实际常用区更窄） */
static int MIN_FREQ = 262;
static int MAX_FREQ = 2093;
static int MIN_NOTE_MS = 80;   /* 普通音符下限 */
static int GAP_MS = 15;        /* 音符间短暂静音 */

/* ============================ 曲目数据 ============================ */
/* 测试旋律：C4 E4 G4 C5（音高/节拍/粘连/刺耳 四项自检） */
static const struct MNote song_test[] = {
    {60,200},{64,200},{67,200},{72,400},{0,250},
    {60,200},{64,200},{67,200},{72,400},{0,250},
};

/* 《5:20AM》(刀酱) —— E 调(1=E4) 竖笛谱还原：前奏动机 + 副歌 hook
 * E: 1=E4(64) 2=F#4(66) 3=G#4(68) 4=A4(69) 5=B4(71) 6=C#5(73) 7=D#5(75)
 * 8分=476ms(♩=63)，前奏动机 16 分=238ms */
static const struct MNote song_520am[] = {
    /* 前奏口哨动机 5 4 7 1 ×2（抖音洗脑 loop） */
    {71,238},{69,238},{75,238},{64,238},
    {71,238},{69,238},{75,238},{64,238},
    {71,238},{69,238},{75,238},{64,238},
    {71,238},{69,238},{75,238},{64,238},
    {0,300},
    /* 慢速动机：5- 1- 7- 1- | 5 4 7 1 */
    {71,952},{64,952},{75,952},{64,952},
    {71,476},{69,476},{75,476},{64,952},
    {0,400},
    /* hook：我在5:20睡觉 13:14准时起 主打个浪漫 沉溺在爱河 不上岸 */
    {68,476},{68,476},{68,476},{71,476},{64,476},
    {66,476},{64,476},{66,476},{66,476},{64,476},{64,476},
    {71,476},{71,476},{64,476},{71,476},
    {71,476},{71,476},{73,476},
    {71,952},
    {71,476},{71,476},{64,476},{64,952},
    {0,500},
    /* hook 重复 */
    {68,476},{68,476},{68,476},{71,476},{64,476},
    {66,476},{64,476},{66,476},{66,476},{64,476},{64,476},
    {71,476},{71,476},{64,476},{71,476},
    {71,476},{71,476},{73,476},
    {71,952},
    {71,476},{71,476},{64,476},{64,952},
};

static const struct MNote song_dji[] = { /* 大疆开机音 C5 D5 G5 */
    {72,300},{74,350},{79,550},
};

/* 《Keil 注册机》(BV1LM4m1R7Xf 同款) —— 从 stm32f103 4通道 tracker 源码提取主旋律
 * 原数据 midi 33-58，整体移调 +24；80ms 行速；取最高音轨合并 */
static const struct MNote song_keil[] = {
    {72,2080},{70,80},{72,240},{70,80},{72,400},{70,80},{72,80},{75,80},
    {72,5440},{67,160},{72,80},{70,80},{69,80},{71,80},{72,80},{71,80},
    {69,80},{64,80},{67,80},{72,880},{71,240},{67,80},{72,400},{71,80},
    {72,720},{70,80},{72,400},{70,80},{72,80},{75,80},{72,880},{69,80},
    {72,880},{71,240},{69,80},{72,400},{71,80},{72,240},{69,80},{72,880},
    {71,240},{69,80},{72,400},{71,80},{72,240},{69,80},{72,880},{71,240},
    {69,80},{72,400},{71,80},{72,2800},{69,2480},{72,2480},{69,7600},
    {72,2800},{70,240},{72,240},{74,80},{72,1360},{69,2480},
    {70,80},{72,240},{70,80},{72,400},{70,80},{72,80},{75,80},{72,880},
    {70,80},{72,240},{70,80},{72,400},{70,80},{72,80},{75,80},{72,880},
    {70,80},{72,240},{70,80},{72,400},{70,80},{72,80},{75,80},{72,880},
    {70,80},{72,240},{70,80},{72,400},{70,80},{72,80},{75,80},{72,880},
    {71,80},{72,880},{71,400},{72,400},{71,80},{72,240},{71,80},{72,880},
    {71,400},{72,400},{71,80},{72,240},{71,80},{72,880},{71,400},{72,400},
    {71,80},{72,240},{71,80},{72,880},{71,400},{72,400},{71,80},{72,240},
    {69,80},{72,880},{71,240},{69,80},{72,400},{71,80},{72,240},{69,80},
    {72,880},{71,240},{69,80},{72,400},{71,80},
};

/* ============================ 时间工具 ============================ */
static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}
static void msleep(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ============================ 方波输出 ============================ */
static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
static void tone_start(int lfd, int freq, int duty_pct, long long *tick,
                       long long *hi_ns, long long *lo_ns) {
    long long period_ns = (long long)(1e9 / (double)freq);
    *hi_ns = period_ns * duty_pct / 100;
    if (*hi_ns < 1) *hi_ns = 1;
    *lo_ns = period_ns - *hi_ns;
    if (*lo_ns < 1) *lo_ns = 1;
    *tick = now_ns();           /* 以 ns 对齐的绝对游标 */
}
/* 忙等翻转：消除 clock_nanosleep 的调度器唤醒抖动（沙哑主因） */
static void tone_next_cycle(int lfd, int high, long long *tick, long long hi_ns, long long lo_ns) {
    struct gpiohandle_data d;
    memset(&d, 0, sizeof(d));
    d.values[0] = high ? 1 : 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d);
    *tick += high ? hi_ns : lo_ns;
    while (now_ns() < *tick) ;  /* spin 到绝对时刻 */
}

/* ============================ 阻塞播放 ============================ */
static int g_duty = 50;

static void play_freq_blocking(int lfd, int freq, int dur_ms) {
    if (freq <= 0 || dur_ms <= 0) { msleep(dur_ms > 0 ? dur_ms : 0); return; }
    long long tick, hi_ns, lo_ns;
    tone_start(lfd, freq, g_duty, &tick, &hi_ns, &lo_ns);
    long long end = tick + (long long)dur_ms * 1000000LL;
    while (tick < end) {
        tone_next_cycle(lfd, 1, &tick, hi_ns, lo_ns);
        if (tick >= end) break;
        tone_next_cycle(lfd, 0, &tick, hi_ns, lo_ns);
    }
    struct gpiohandle_data d;         /* 音符结束引脚拉低 */
    memset(&d, 0, sizeof(d));
    d.values[0] = 0;
    ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d);
}

static void play_song_blocking(int lfd, const struct MNote *seq, int len,
                               double tempo, int transpose) {
    for (int i = 0; i < len; i++) {
        int midi = seq[i].midi;
        int dur = (int)(seq[i].dur_ms / tempo);
        if (dur < MIN_NOTE_MS) dur = MIN_NOTE_MS;
        int freq = midi_to_freq(midi > 0 ? midi + transpose : 0);
        if (freq > 0) {                /* 音域钳制 */
            if (freq < MIN_FREQ) freq = MIN_FREQ;
            if (freq > MAX_FREQ) freq = MAX_FREQ;
        }
        play_freq_blocking(lfd, freq, dur);
        if (freq > 0 && i + 1 < len) msleep(GAP_MS);
    }
}

/* ============================ 非阻塞状态机 ============================ */
/* 状态机驱动版：由外部循环每 slice_ms 调用一次 tick，每次只忙等一小片，
   主程序不会被整首歌的 delay 阻塞；移植到嵌入式时把 tick 放定时器中断。 */
typedef struct {
    const struct MNote *seq; int len; int idx;
    int state;              /* 0=NOTE 1=GAP 2=DONE */
    long long t_next;       /* 状态切换的绝对时间 */
    int freq;
} BuzzPlayer;

static void buzz_init(BuzzPlayer *p, const struct MNote *seq, int len) {
    p->seq = seq; p->len = len; p->idx = 0;
    p->state = 0; p->t_next = 0; p->freq = 0;
}
static void buzz_next_note(BuzzPlayer *p, double tempo, int transpose) {
    if (p->idx >= p->len) { p->state = 2; return; }
    int midi = p->seq[p->idx].midi;
    int dur = (int)(p->seq[p->idx].dur_ms / tempo);
    if (dur < MIN_NOTE_MS) dur = MIN_NOTE_MS;
    p->freq = midi_to_freq(midi > 0 ? midi + transpose : 0);
    if (p->freq > 0) {
        if (p->freq < MIN_FREQ) p->freq = MIN_FREQ;
        if (p->freq > MAX_FREQ) p->freq = MAX_FREQ;
    }
    p->t_next = now_ms() + dur;
    p->state = 0;
}
/* 返回 1=播放完成 */
static int buzz_tick(BuzzPlayer *p, int lfd, int slice_ms, double tempo, int transpose) {
    if (p->state == 2) return 1;
    long long nw = now_ms();
    if (p->idx == 0 && p->t_next == 0) buzz_next_note(p, tempo, transpose);
    if (nw >= p->t_next) {
        if (p->state == 0) {           /* NOTE -> GAP */
            struct gpiohandle_data d;
            memset(&d, 0, sizeof(d));
            d.values[0] = 0;
            ioctl(lfd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d);
            p->state = 1;
            p->t_next = nw + GAP_MS;
            if (p->freq == 0) p->idx++;
        } else {                        /* GAP -> next NOTE */
            p->idx++;
            buzz_next_note(p, tempo, transpose);
            if (p->state == 2) { p->t_next = nw; return 1; }
        }
    }
    if (p->state == 0 && p->freq > 0) { /* 本 slice 内忙等输出方波 */
        long long end = nw + slice_ms;
        if (end > p->t_next) end = p->t_next;
        if (end > nw) {
            long long tick = nw * 1000000LL, hi_ns, lo_ns;
            tone_start(lfd, p->freq, g_duty, &tick, &hi_ns, &lo_ns);
            while (tick < end * 1000000LL) {
                tone_next_cycle(lfd, 1, &tick, hi_ns, lo_ns);
                if (tick >= end * 1000000LL) break;
                tone_next_cycle(lfd, 0, &tick, hi_ns, lo_ns);
            }
        }
    }
    return 0;
}

/* ============================ 入口 ============================ */
static int open_line(int line) {
    int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) { perror("open gpiochip0"); return -1; }
    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = line;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.lines = 1;
    strncpy(req.consumer_label, "buzzer", sizeof(req.consumer_label) - 1);
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        perror("GPIO_GET_LINEHANDLE_IOCTL"); close(fd); return -1;
    }
    close(fd);
    return req.fd;
}

static const struct MNote *find_song(const char *name, int *len) {
    if (!strcmp(name, "test")) { *len = sizeof(song_test)/sizeof(song_test[0]); return song_test; }
    if (!strcmp(name, "520am")) { *len = sizeof(song_520am)/sizeof(song_520am[0]); return song_520am; }
    if (!strcmp(name, "dji")) { *len = sizeof(song_dji)/sizeof(song_dji[0]); return song_dji; }
    if (!strcmp(name, "keil")) { *len = sizeof(song_keil)/sizeof(song_keil[0]); return song_keil; }
    return NULL;
}

int main(int argc, char **argv) {
    const char *songname = "test";
    int line = 23, nonblock = 0;
    double tempo = 1.0;
    int transpose = 0;
    if (argc > 1) songname = argv[1];
    if (argc > 2) g_duty = atoi(argv[2]);
    if (argc > 3) line = atoi(argv[3]);
    if (argc > 4) tempo = atof(argv[4]);
    if (argc > 5) transpose = atoi(argv[5]);
    if (argc > 6 && !strcmp(argv[6], "-nb")) nonblock = 1;
    if (g_duty < 2) g_duty = 2;
    if (g_duty > 50) g_duty = 50;
    if (line < 0 || line > 31) line = 23;
    if (tempo <= 0) tempo = 1.0;

    int len = 0;
    const struct MNote *seq = find_song(songname, &len);
    if (!seq) {
        printf("unknown song: %s (test|520am|dji|keil)\n", songname);
        return 1;
    }
    int lfd = open_line(line);
    if (lfd < 0) return 1;

    printf("buzzer_play song=%s duty=%d%% line=%d tempo=%.2f transpose=%+d %s\n",
           songname, g_duty, line, tempo, transpose, nonblock ? "(nonblock)" : "");

    if (nonblock) {
        BuzzPlayer p;
        buzz_init(&p, seq, len);
        while (!buzz_tick(&p, lfd, 5, tempo, transpose)) { /* 每 5ms 片 */ }
    } else {
        play_song_blocking(lfd, seq, len, tempo, transpose);
    }
    close(lfd);
    printf("done\n");
    return 0;
}
