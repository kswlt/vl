/* Runtime adapter for deployed vizd binaries built before the isom fix.
 * The bundled FFmpeg has a MOV muxer but no separate MP4 muxer. Passing
 * -brand isom makes its fragmented output compatible with browser MP4 parsers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FFMPEG_REAL "/userdata/xtapp/ffmpeg_fmp4.real"

int main(int argc, char **argv) {
    const int mov_output = argc >= 4 &&
        strcmp(argv[argc - 3], "-f") == 0 &&
        strcmp(argv[argc - 2], "mov") == 0 &&
        strcmp(argv[argc - 1], "pipe:1") == 0;
    int has_brand = 0;
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "-brand") == 0) has_brand = 1;

    char **command = calloc((size_t)argc + 3, sizeof(*command));
    if (!command) return 1;
    int out = 0;
    command[out++] = argv[0];
    for (int i = 1; i < argc; ++i) {
        if (mov_output && !has_brand && i == argc - 3) {
            command[out++] = "-brand";
            command[out++] = "isom";
        }
        command[out++] = argv[i];
    }
    command[out] = NULL;
    execv(FFMPEG_REAL, command);
    perror("execv ffmpeg_fmp4.real");
    return 127;
}
