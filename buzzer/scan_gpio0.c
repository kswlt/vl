/*
 * scan_gpio0.c — sweep all free lines of gpiochip0 (GPIO0) HIGH one by one,
 * so the physical header pin can be identified by watching a multimeter.
 * Usage: scan_gpio0 [hold_ms]   (default 1200 ms per line)
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

static void msleep(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int main(int argc, char** argv) {
    long hold_ms = 1200;
    if (argc > 1) hold_ms = atol(argv[1]);
    if (hold_ms < 200 || hold_ms > 5000) hold_ms = 1200;

    int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) { perror("open gpiochip0"); return 1; }

    for (int line = 0; line < 32; line++) {
        struct gpiohandle_request req;
        memset(&req, 0, sizeof(req));
        req.lineoffsets[0] = line;
        req.flags = GPIOHANDLE_REQUEST_OUTPUT;
        req.default_values[0] = 1;      /* start HIGH */
        req.lines = 1;
        snprintf(req.consumer_label, sizeof(req.consumer_label), "scan");
        if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
            printf("line %2d: BUSY (%s)\n", line, strerror(errno));
            continue;
        }
        printf("line %2d: HIGH for %ld ms\n", line, hold_ms);
        fflush(stdout);
        msleep(hold_ms);
        close(req.fd);
    }
    close(fd);
    printf("scan done\n");
    return 0;
}
