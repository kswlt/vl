/******************************************************************************
 * xt_camera.cpp - MC800S native JPEG/H264 -> Space Camera Protocol V1.0
 *
 * Data channel:
 *   active/{sn}/image  ImageMsgArray  format=JPEG(2) or H264(3)
 *
 * Payload:
 *   ImageMsgArray.array has exactly one ImageMsg.
 *   JPEG data is one complete native snapshot, unmodified.
 *   H264 data is one complete native access unit in Annex-B form.
 ******************************************************************************/
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "device_config.hpp"
#include "device_sn.hpp"
#include "zenoh-pico.h"
#include "protocol_clock.hpp"
#include "h264_annexb.hpp"
#include "snapshot_client.hpp"

static const char *CAMERA_IP = "192.0.2.123";
static const int CAMERA_PORT = 80;
static const char *CAM_USER = getenv("XT_CAMERA_USER") ? getenv("XT_CAMERA_USER") : "admin";
static const char *CAM_PASSWORD = getenv("XT_CAMERA_PASSWORD");
static const char *CAM_PASS_MD5 = getenv("XT_CAMERA_PASSWORD_MD5");
static const int DEFAULT_SNAPSHOT_STREAM = 1; // MC800S: 1=1920x1080, 0=720x480
static const int IMAGE_FORMAT_JPEG = 2;
static const int IMAGE_FORMAT_H264 = 3;
static const int H264_WIDTH = 1920;
static const int H264_HEIGHT = 1080;
static int TARGET_HZ = 10;
static int IMAGE_MODE = IMAGE_FORMAT_JPEG;
static int H264_GOP = 30;
static int H264_BITRATE_BPS = 8 * 1024 * 1024;
static bool H264_NATIVE_PASSTHROUGH = false;
static const char *SN_FILE = getenv("XT_DEVICE_SN_FILE") ? getenv("XT_DEVICE_SN_FILE") : "/mnt/system/factory-data/Legacy.yaml";
static std::string g_sn = "UNCONFIGURED";
static const char *RAW_DIR = "/userdata/xtapp/raw_camera";
static const int RAW_WINDOW_SEC = 300;
static const int RAW_SHARD_SEC = 60;

static std::atomic<bool> g_running{true};
static std::atomic<uint32_t> g_seq{0};
static const char *IMAGE_INGEST_SOCKET = "/tmp/xt_image_ingest.sock";
static int g_ingest_fd = -1;

struct Frame {
    std::vector<uint8_t> data;
    uint32_t format = IMAGE_FORMAT_JPEG;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t stamp_us = 0;
    uint64_t capture_id = 0;
    int64_t fetched_ns = 0;
    double http_ms = 0;
};

static std::mutex g_q_mtx;
static std::condition_variable g_q_cv;
static std::deque<Frame> g_queue;
static const size_t QUEUE_MAX = 3;

static FILE *g_rawfile = NULL;
static char g_rawpath[512] = {0};
static time_t g_rawshard = 0;
static bool g_raw_enabled = false;

static int snapshot_stream() {
    const char *v = getenv("XT_CAMERA_SNAPSHOT_STREAM");
    if (!v || !*v) return DEFAULT_SNAPSHOT_STREAM;
    int s = atoi(v);
    return s == 0 ? 0 : 1;
}

static bool env_enabled(const char *name) {
    const char *v = getenv(name);
    return v && (*v == '1' || *v == 'y' || *v == 'Y' || *v == 't' || *v == 'T');
}

static bool jpeg_transcode_enabled(const xt::Json &sensors) {
    return sensors.value("image_jpeg_source", "snapshot") == "transcode";
}

static int jpeg_quality() {
    const char *v = getenv("XT_CAMERA_JPEG_QUALITY");
    if (!v || !*v) return 5;
    int q = atoi(v);
    return q < 1 ? 1 : q > 31 ? 31 : q;
}

static bool write_all(int fd, const uint8_t *data, size_t length) {
    while (length) {
        ssize_t written = send(fd, data, length, MSG_NOSIGNAL);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        data += written;
        length -= size_t(written);
    }
    return true;
}

static int send_image_payload(const std::vector<uint8_t> &payload) {
    if (payload.empty() || payload.size() > 1024 * 1024) return -1;
    if (g_ingest_fd < 0) {
        g_ingest_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (g_ingest_fd < 0) return -1;
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        strncpy(address.sun_path, IMAGE_INGEST_SOCKET, sizeof(address.sun_path) - 1);
        if (connect(g_ingest_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
            close(g_ingest_fd);
            g_ingest_fd = -1;
            return -1;
        }
        printf("[cam] image handoff connected %s\n", IMAGE_INGEST_SOCKET);
    }
    const uint32_t length = htonl(uint32_t(payload.size()));
    if (write_all(g_ingest_fd, reinterpret_cast<const uint8_t *>(&length), sizeof(length)) &&
        write_all(g_ingest_fd, payload.data(), payload.size())) return 0;
    close(g_ingest_fd);
    g_ingest_fd = -1;
    return -1;
}

static int64_t monotonic_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void load_sn() {
    xt::load_existing_device_sn(SN_FILE, g_sn);
    printf("[cfg] device_sn=%s\n", g_sn.c_str());
}

static void raw_mkdir(void) { mkdir(RAW_DIR, 0777); }

static void raw_cleanup(time_t now) {
    time_t cutoff = now - RAW_WINDOW_SEC - 60;
    DIR *d = opendir(RAW_DIR);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "raw-", 4) != 0) continue;
        char fp[700];
        snprintf(fp, sizeof fp, "%s/%s", RAW_DIR, e->d_name);
        struct stat st;
        if (stat(fp, &st) == 0 && st.st_mtime < cutoff) remove(fp);
    }
    closedir(d);
}

static void raw_open_shard(time_t now) {
    if (g_rawfile && now >= g_rawshard && now < g_rawshard + RAW_SHARD_SEC) return;
    if (g_rawfile) { fclose(g_rawfile); g_rawfile = NULL; }
    struct tm tmv;
    localtime_r(&now, &tmv);
    char name[64];
    strftime(name, sizeof name, "raw-%Y%m%d-%H%M%S.bin", &tmv);
    snprintf(g_rawpath, sizeof g_rawpath, "%s/%s", RAW_DIR, name);
    g_rawfile = fopen(g_rawpath, "ab");
    g_rawshard = now;
    printf("[raw] new shard: %s\n", g_rawpath);
}

static size_t raw_append(int64_t ts_ns, const uint8_t *data, size_t len) {
    static uint32_t flush_count = 0;
    if (!g_raw_enabled) return 0;
    raw_mkdir();
    time_t now = time(NULL);
    raw_cleanup(now);
    raw_open_shard(now);
    if (!g_rawfile) return 0;
    uint8_t hdr[12];
    memcpy(hdr, &ts_ns, 8);
    uint32_t l = (uint32_t)len;
    memcpy(hdr + 8, &l, 4);
    fwrite(hdr, 1, 12, g_rawfile);
    size_t w = fwrite(data, 1, len, g_rawfile);
    if ((++flush_count % 10) == 0) fflush(g_rawfile);
    return 12 + w;
}

class PBWriter {
public:
    std::vector<uint8_t> buf;
    void varint(uint64_t v) {
        while (v >= 0x80) { buf.push_back((uint8_t)(v & 0x7f) | 0x80); v >>= 7; }
        buf.push_back((uint8_t)v);
    }
    void tag(int field, int wire) { varint(((uint64_t)field << 3) | (uint64_t)wire); }
    void u32(int field, uint32_t v) { if (v == 0) return; tag(field, 0); varint(v); }
    void s64(int field, int64_t v) { if (v == 0) return; tag(field, 0); varint(zigzag(v)); }
    void len(int field, const uint8_t *p, size_t n) { tag(field, 2); varint(n); buf.insert(buf.end(), p, p + n); }
    static uint64_t zigzag(int64_t v) { return (uint64_t(v) << 1) ^ uint64_t(-(v < 0)); }
};

static std::vector<uint8_t> build_image_array_msg(const Frame &f, uint32_t seq) {
    PBWriter arr;
    PBWriter m;
    PBWriter h;
    h.u32(1, seq);
    h.s64(2, f.stamp_us);
    h.s64(3, 0);
    m.len(1, h.buf.data(), h.buf.size());
    m.u32(2, f.format);
    m.u32(3, f.width);
    m.u32(4, f.height);
    m.len(5, f.data.data(), f.data.size());
    arr.len(1, m.buf.data(), m.buf.size());
    return arr.buf;
}

static bool parse_jpeg_size(const std::vector<uint8_t> &jpeg, uint32_t &w, uint32_t &h) {
    if (jpeg.size() < 4 || jpeg[0] != 0xff || jpeg[1] != 0xd8) return false;
    size_t i = 2;
    while (i + 9 < jpeg.size()) {
        if (jpeg[i] != 0xff) { i++; continue; }
        while (i < jpeg.size() && jpeg[i] == 0xff) i++;
        if (i >= jpeg.size()) return false;
        uint8_t marker = jpeg[i++];
        if (marker == 0xd9 || marker == 0xda) return false;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        if (i + 2 > jpeg.size()) return false;
        uint16_t seg_len = ((uint16_t)jpeg[i] << 8) | jpeg[i + 1];
        if (seg_len < 2 || i + seg_len > jpeg.size()) return false;
        if ((marker >= 0xc0 && marker <= 0xc3) || (marker >= 0xc5 && marker <= 0xc7) ||
            (marker >= 0xc9 && marker <= 0xcb) || (marker >= 0xcd && marker <= 0xcf)) {
            if (seg_len < 7) return false;
            h = ((uint16_t)jpeg[i + 3] << 8) | jpeg[i + 4];
            w = ((uint16_t)jpeg[i + 5] << 8) | jpeg[i + 6];
            return w > 0 && h > 0;
        }
        i += seg_len;
    }
    return false;
}

static size_t append_text(char *data, size_t size, size_t count, void *ctx) {
    const size_t bytes = size * count;
    static_cast<std::string *>(ctx)->append(data, bytes);
    return bytes;
}

static bool configure_native_h264(const xt::Json &sensors) {
    const int output_gop = sensors["h264_gop"].get<int>();
    // MC800S clamps an ONVIF GOP shorter than one second to its frame rate.
    // A shorter GOP for the strict output encoder must not be sent upstream.
    const int gop = !H264_NATIVE_PASSTHROUGH && IMAGE_MODE == IMAGE_FORMAT_H264 &&
                    output_gop < TARGET_HZ ? TARGET_HZ : output_gop;
    const int bitrate_mbps = sensors["h264_bitrate"].get<int>();
    if (gop == 0 || bitrate_mbps == 0) {
        printf("[h264] ONVIF encoder values left unchanged gop=%d bitrate=%dMbps\n", gop, bitrate_mbps);
        return true;
    }
    const int bitrate_kbps = bitrate_mbps * 1024;
    const std::string body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
        "xmlns:tt=\"http://www.onvif.org/ver10/schema\"><s:Body>"
        "<trt:SetVideoEncoderConfiguration><trt:Configuration token=\"VideoEncodeMain\">"
        "<tt:Name>VideoEncodeMain</tt:Name><tt:UseCount>1</tt:UseCount><tt:Encoding>H264</tt:Encoding>"
        "<tt:Resolution><tt:Width>" + std::to_string(H264_WIDTH) + "</tt:Width><tt:Height>" +
        std::to_string(H264_HEIGHT) + "</tt:Height></tt:Resolution>"
        "<tt:Quality>50</tt:Quality><tt:RateControl><tt:FrameRateLimit>" + std::to_string(TARGET_HZ) +
        "</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>" +
        std::to_string(bitrate_kbps) + "</tt:BitrateLimit></tt:RateControl><tt:H264><tt:GovLength>" +
        std::to_string(gop) + "</tt:GovLength><tt:H264Profile>High</tt:H264Profile></tt:H264>"
        "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>192.0.2.123"
        "</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port><tt:TTL>0</tt:TTL>"
        "<tt:AutoStart>false</tt:AutoStart></tt:Multicast><tt:SessionTimeout>PT00H12M00S"
        "</tt:SessionTimeout></trt:Configuration><trt:ForcePersistence>true</trt:ForcePersistence>"
        "</trt:SetVideoEncoderConfiguration></s:Body></s:Envelope>";

    CURL *curl = curl_easy_init();
    if (!curl) return false;
    std::string response;
    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/soap+xml; charset=utf-8");
    curl_easy_setopt(curl, CURLOPT_URL, "http://192.0.2.123/Media");
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(curl, CURLOPT_USERNAME, CAM_USER);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, CAM_PASSWORD);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_text);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    CURLcode result = CURLE_OK;
    long status = 0;
    bool ok = false;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        response.clear();
        result = curl_easy_perform(curl);
        status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        ok = result == CURLE_OK && status == 200 &&
             response.find("Fault") == std::string::npos;
        if (ok) break;
        printf("[h264] ONVIF configuration attempt=%d result=%d status=%ld\n",
               attempt, int(result), status);
        if (attempt < 3) sleep(1);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    printf("[h264] ONVIF source request %dx%d %dHz requested_camera_gop=%d bitrate=%dkbps "
           "result=%d status=%ld configured=%d\n",
           H264_WIDTH, H264_HEIGHT, TARGET_HZ, gop, bitrate_kbps, int(result), status, ok ? 1 : 0);
    return ok;
}


static void push_frame(Frame &&f) {
    std::lock_guard<std::mutex> lk(g_q_mtx);
    while (g_queue.size() >= QUEUE_MAX) g_queue.pop_front();
    g_queue.push_back(std::move(f));
    g_q_cv.notify_one();
}

static void capture_jpeg_thread_main() {
    int stream = snapshot_stream();
    printf("[cap] snapshot stream=%d target=%dHz\n", stream, TARGET_HZ);
    std::string url = std::string("http://") + CAMERA_IP + ":" + std::to_string(CAMERA_PORT) +
        "/cgi-bin/snapshot.cgi?stream=" + std::to_string(stream) + "&username=" + CAM_USER + "&password=" + CAM_PASS_MD5;
    HttpSnapshotClient cli(url);
    uint64_t captures = 0, fails = 0;
    double http_sum=0, http_max=0, first_sum=0;
    uint64_t new_connections=0;
    auto period = std::chrono::nanoseconds(1000000000 / TARGET_HZ);
    auto next = std::chrono::steady_clock::now();

    while (g_running.load()) {
        next += period;
        std::vector<uint8_t> jpeg;
        uint32_t w = 0, h = 0;
        const bool success = cli.get(jpeg) && parse_jpeg_size(jpeg, w, h);
        if (success) {
            Frame f;
            f.data = std::move(jpeg);
            f.format = IMAGE_FORMAT_JPEG;
            f.width = w;
            f.height = h;
            f.stamp_us = xt::protocol_timestamp_us();
            f.capture_id = ++captures;
            f.fetched_ns = monotonic_ns();
            f.http_ms = cli.total_ms;
            push_frame(std::move(f));
        } else {
            fails++;
            if (fails <= 5 || fails % 20 == 0)
                printf("[cap] request failed curl=%d status=%ld http_ms=%.1f\n", int(cli.result), cli.status, cli.total_ms);
        }
        http_sum += cli.total_ms;
        first_sum += cli.first_byte_ms;
        http_max = std::max(http_max, cli.total_ms);
        new_connections += cli.connections;
        if (((captures + fails) % 100) == 0) {
            std::lock_guard<std::mutex> lk(g_q_mtx);
            printf("[cap] ok=%llu fail=%llu queue=%zu http_avg_ms=%.2f first_avg_ms=%.2f http_max_ms=%.2f connects=%llu/100\n",
                   (unsigned long long)captures, (unsigned long long)fails, g_queue.size(),
                   http_sum/100, first_sum/100, http_max, (unsigned long long)new_connections);
            http_sum=http_max=first_sum=0; new_connections=0;
        }
        // Do not accumulate catch-up debt or replay queued old frames after an outage.
        auto now = std::chrono::steady_clock::now();
        if (next < now) next = now;
        if (!success) next = now + std::chrono::milliseconds(50);
        std::this_thread::sleep_until(next);
    }
}

class GstreamerH264Client {
public:
    explicit GstreamerH264Client(std::string uri, bool passthrough)
        : uri_(std::move(uri)), passthrough_(passthrough) {}
    ~GstreamerH264Client() { stop(); }

    bool start() {
        stop();
        int fds[2];
        if (pipe(fds) != 0) return false;
        const pid_t child = fork();
        if (child < 0) { close(fds[0]); close(fds[1]); return false; }
        if (child == 0) {
            dup2(fds[1], STDOUT_FILENO);
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDERR_FILENO);
            }
            for (int fd = 3; fd < 1024; ++fd) close(fd);
            char location[512], gop[32], bitrate[32];
            snprintf(location, sizeof location, "location=%s", uri_.c_str());
            snprintf(gop, sizeof gop, "gop=%d", H264_GOP);
            snprintf(bitrate, sizeof bitrate, "bps=%d", H264_BITRATE_BPS);
            const char *program = "/usr/bin/gst-launch-1.0";
            const char *native_args[] = {program, "-q", "rtspsrc", location, "protocols=tcp", "latency=50",
                "!", "rtph264depay", "!", "h264parse", "config-interval=-1", "!",
                "video/x-h264,stream-format=byte-stream,alignment=au", "!",
                "fdsink", "fd=1", "sync=false", nullptr};
            const char *strict_args[] = {program, "-q", "rtspsrc", location, "protocols=tcp", "latency=50",
                "!", "rtph264depay", "!", "h264parse", "!", "mppvideodec", "!",
                "mpph264enc", gop, bitrate, "header-mode=each-idr", "rc-mode=cbr", "profile=high",
                "!", "h264parse", "config-interval=-1", "!",
                "video/x-h264,stream-format=byte-stream,alignment=au", "!",
                "fdsink", "fd=1", "sync=false", nullptr};
            execv(program, const_cast<char *const *>(passthrough_ ? native_args : strict_args));
            _exit(127);
        }
        close(fds[1]);
        fd_ = fds[0];
        pid_ = child;
        parser_.reset();
        return true;
    }

    bool next(std::vector<uint8_t> &access_unit, uint32_t &width, uint32_t &height,
              int timeout_ms = 4000) {
        access_unit.clear();
        while (g_running.load()) {
            if (parser_.pop(access_unit)) {
                width = parser_.width();
                height = parser_.height();
                return width > 0 && height > 0 && !access_unit.empty();
            }
            pollfd pfd{fd_, POLLIN, 0};
            int result;
            do { result = poll(&pfd, 1, timeout_ms); } while (result < 0 && errno == EINTR);
            if (result <= 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) return false;
            uint8_t chunk[64 * 1024];
            ssize_t got;
            do { got = read(fd_, chunk, sizeof chunk); } while (got < 0 && errno == EINTR);
            if (got <= 0) return false;
            parser_.feed(chunk, static_cast<size_t>(got));
        }
        return false;
    }

    void stop() {
        if (fd_ >= 0) { close(fd_); fd_ = -1; }
        if (pid_ <= 0) return;
        kill(pid_, SIGTERM);
        int status = 0;
        for (int i = 0; i < 20; ++i) {
            if (waitpid(pid_, &status, WNOHANG) == pid_) { pid_ = -1; return; }
            usleep(25000);
        }
        kill(pid_, SIGKILL);
        waitpid(pid_, &status, 0);
        pid_ = -1;
    }

private:
    std::string uri_;
    bool passthrough_ = false;
    int fd_ = -1;
    pid_t pid_ = -1;
    AnnexBAccessUnitParser parser_;
};

// Splits a concatenated JPEG stream (image2pipe) into complete JPEG frames
// delimited by SOI (FFD8) .. EOI (FFD9).
class JpegFrameParser {
public:
    void reset() { buf_.clear(); }
    void feed(const uint8_t *p, size_t n) { buf_.insert(buf_.end(), p, p + n); }
    bool pop(std::vector<uint8_t> &frame) {
        size_t start = std::string::npos;
        for (size_t i = 0; i + 1 < buf_.size(); ++i) {
            if (buf_[i] == 0xff && buf_[i + 1] == 0xd8) { start = i; break; }
        }
        if (start == std::string::npos) { buf_.clear(); return false; }
        if (start > 0) buf_.erase(buf_.begin(), buf_.begin() + start);
        for (size_t i = 1; i + 1 < buf_.size(); ++i) {
            if (buf_[i] == 0xff && buf_[i + 1] == 0xd9) {
                frame.assign(buf_.begin(), buf_.begin() + i + 2);
                buf_.erase(buf_.begin(), buf_.begin() + i + 2);
                return true;
            }
        }
        return false;
    }
private:
    std::vector<uint8_t> buf_;
};

// Some Rockchip mppjpegenc builds emit two JPEG headers in one frame:
// SOI/APP0/DQT/.../SOS followed immediately by DQT/.../SOS and image data.
// Strict decoders (including browsers and libjpeg) reject the second SOF.
// Keep the first SOI/APP0 and the complete second header; do not recompress.
static bool normalize_mpp_jpeg_header(std::vector<uint8_t> &jpeg) {
    if (jpeg.size() < 64 || jpeg[0] != 0xff || jpeg[1] != 0xd8) return false;
    size_t at = 2, first_dqt = 0, second_header = 0;
    while (at + 4 < jpeg.size() && jpeg[at] == 0xff) {
        const size_t marker_start = at;
        while (at < jpeg.size() && jpeg[at] == 0xff) ++at;
        if (at >= jpeg.size()) return false;
        const uint8_t marker = jpeg[at++];
        if (marker == 0xd8 || marker == 0xd9 || marker == 0x00 ||
            (marker >= 0xd0 && marker <= 0xd7)) return false;
        if (at + 2 > jpeg.size()) return false;
        const size_t length = (size_t(jpeg[at]) << 8) | jpeg[at + 1];
        if (length < 2 || length > jpeg.size() - at) return false;
        if (marker == 0xdb && first_dqt == 0) first_dqt = marker_start;
        at += length;
        if (marker == 0xda) {
            if (first_dqt && at + 4 < jpeg.size() && jpeg[at] == 0xff &&
                jpeg[at + 1] == 0xdb) second_header = at;
            break;
        }
    }
    if (!second_header) return true; // Ordinary JPEG.
    size_t scan = second_header;
    bool second_sof = false, second_sos = false;
    while (scan + 4 < jpeg.size() && scan < second_header + 2048 && jpeg[scan] == 0xff) {
        while (scan < jpeg.size() && jpeg[scan] == 0xff) ++scan;
        if (scan >= jpeg.size()) return false;
        const uint8_t marker = jpeg[scan++];
        if (scan + 2 > jpeg.size()) return false;
        const size_t length = (size_t(jpeg[scan]) << 8) | jpeg[scan + 1];
        if (length < 2 || length > jpeg.size() - scan) return false;
        if (marker == 0xc0) second_sof = true;
        scan += length;
        if (marker == 0xda) { second_sos = true; break; }
    }
    if (!second_sof || !second_sos || first_dqt >= second_header) return false;
    jpeg.erase(jpeg.begin() + first_dqt, jpeg.begin() + second_header);
    return true;
}

class MppJpegClient {
public:
    explicit MppJpegClient(std::string uri) : uri_(std::move(uri)) {}
    ~MppJpegClient() { stop(); }

    bool start() {
        stop();
        int fds[2];
        if (pipe(fds) != 0) return false;
        const pid_t child = fork();
        if (child < 0) { close(fds[0]); close(fds[1]); return false; }
        if (child == 0) {
            dup2(fds[1], STDOUT_FILENO);
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDERR_FILENO);
            }
            for (int fd = 3; fd < 1024; ++fd) close(fd);
            const char *program = "/usr/bin/gst-launch-1.0";
            char location[512], quality[32];
            snprintf(location, sizeof location, "location=%s", uri_.c_str());
            // FFmpeg q:v is inverse (lower is better); MPP q-factor is a
            // direct quality percentage. Keep the default q=5 near quality 85.
            const int q_factor = std::clamp(100 - jpeg_quality() * 3, 5, 95);
            snprintf(quality, sizeof quality, "q-factor=%d", q_factor);
            const char *args[] = {program, "-q", "rtspsrc", location,
                "protocols=tcp", "latency=50", "!", "rtph264depay", "!",
                "h264parse", "!", "mppvideodec", "!", "queue",
                "max-size-buffers=4", "leaky=downstream", "!", "mppjpegenc",
                quality, "!", "fdsink", "fd=1", "sync=false", nullptr};
            execv(program, const_cast<char *const *>(args));
            _exit(127);
        }
        close(fds[1]);
        fd_ = fds[0];
        pid_ = child;
        parser_.reset();
        return true;
    }

    bool next(std::vector<uint8_t> &jpeg, uint32_t &width, uint32_t &height,
              int timeout_ms = 4000) {
        jpeg.clear();
        while (g_running.load()) {
            if (parser_.pop(jpeg)) {
                width = 0;
                height = 0;
                return normalize_mpp_jpeg_header(jpeg) &&
                    parse_jpeg_size(jpeg, width, height);
            }
            pollfd pfd{fd_, POLLIN, 0};
            int result;
            do { result = poll(&pfd, 1, timeout_ms); } while (result < 0 && errno == EINTR);
            if (result <= 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) return false;
            uint8_t chunk[64 * 1024];
            ssize_t got;
            do { got = read(fd_, chunk, sizeof chunk); } while (got < 0 && errno == EINTR);
            if (got <= 0) return false;
            parser_.feed(chunk, static_cast<size_t>(got));
        }
        return false;
    }

    void stop() {
        if (fd_ >= 0) { close(fd_); fd_ = -1; }
        if (pid_ <= 0) return;
        kill(pid_, SIGTERM);
        int status = 0;
        for (int i = 0; i < 20; ++i) {
            if (waitpid(pid_, &status, WNOHANG) == pid_) { pid_ = -1; return; }
            usleep(25000);
        }
        kill(pid_, SIGKILL);
        waitpid(pid_, &status, 0);
        pid_ = -1;
    }

private:
    std::string uri_;
    int fd_ = -1;
    pid_t pid_ = -1;
    JpegFrameParser parser_;
};

static void capture_h264_thread_main() {
    const std::string uri = std::string("rtsp://") + CAMERA_IP + ":554/stream0?username=" +
        CAM_USER + "&password=" + CAM_PASS_MD5;
    GstreamerH264Client client(uri, H264_NATIVE_PASSTHROUGH);
    uint64_t captures = 0, failures = 0, reconnects = 0;
    printf("[cap] H264 RTSP mode=%s target=%dHz output GOP=%d bitrate=%dbps\n",
           H264_NATIVE_PASSTHROUGH ? "native passthrough" : "strict IDR transcode",
           TARGET_HZ, H264_GOP, H264_BITRATE_BPS);
    while (g_running.load()) {
        if (!client.start()) {
            ++failures;
            printf("[cap] H264 gstreamer start failed, retry in 1s\n");
            sleep(1);
            continue;
        }
        ++reconnects;
        printf("[cap] H264 RTSP connected attempt=%llu\n", (unsigned long long)reconnects);
        while (g_running.load()) {
            std::vector<uint8_t> access_unit;
            uint32_t width = 0, height = 0;
            const int64_t begin = monotonic_ns();
            if (!client.next(access_unit, width, height)) {
                ++failures;
                printf("[cap] H264 stream stalled/closed failures=%llu, reconnecting\n",
                       (unsigned long long)failures);
                break;
            }
            Frame f;
            f.data = std::move(access_unit);
            f.format = IMAGE_FORMAT_H264;
            f.width = width;
            f.height = height;
            f.stamp_us = xt::protocol_timestamp_us();
            f.capture_id = ++captures;
            f.fetched_ns = monotonic_ns();
            f.http_ms = (f.fetched_ns - begin) / 1e6;
            push_frame(std::move(f));
            if ((captures % 100) == 0) {
                std::lock_guard<std::mutex> lk(g_q_mtx);
                printf("[cap] H264 frames=%llu failures=%llu queue=%zu reconnects=%llu\n",
                       (unsigned long long)captures, (unsigned long long)failures, g_queue.size(),
                       (unsigned long long)reconnects);
            }
        }
        client.stop();
        if (g_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

static void capture_jpeg_transcode_thread_main() {
    const std::string uri = std::string("rtsp://") + CAMERA_IP + ":554/stream0?username=" +
        CAM_USER + "&password=" + CAM_PASS_MD5;
    MppJpegClient client(uri);
    uint64_t captures = 0, failures = 0, reconnects = 0;
    printf("[cap] JPEG transcode: H264 RTSP -> MPP JPEG q-factor=%d target=%dHz\n",
           std::clamp(100 - jpeg_quality() * 3, 5, 95), TARGET_HZ);
    while (g_running.load()) {
        if (!client.start()) {
            ++failures;
            printf("[cap] MPP JPEG pipeline start failed, retry in 1s\n");
            sleep(1);
            continue;
        }
        ++reconnects;
        printf("[cap] JPEG RTSP connected attempt=%llu\n", (unsigned long long)reconnects);
        while (g_running.load()) {
            std::vector<uint8_t> jpeg;
            uint32_t width = 0, height = 0;
            const int64_t begin = monotonic_ns();
            if (!client.next(jpeg, width, height)) {
                ++failures;
                printf("[cap] JPEG stream stalled/closed failures=%llu, reconnecting\n",
                       (unsigned long long)failures);
                break;
            }
            Frame f;
            f.data = std::move(jpeg);
            f.format = IMAGE_FORMAT_JPEG;
            f.width = width;
            f.height = height;
            f.stamp_us = xt::protocol_timestamp_us();
            f.capture_id = ++captures;
            f.fetched_ns = monotonic_ns();
            f.http_ms = (f.fetched_ns - begin) / 1e6;
            push_frame(std::move(f));
            if ((captures % 100) == 0) {
                std::lock_guard<std::mutex> lk(g_q_mtx);
                printf("[cap] JPEG frames=%llu failures=%llu queue=%zu reconnects=%llu\n",
                       (unsigned long long)captures, (unsigned long long)failures, g_queue.size(),
                       (unsigned long long)reconnects);
            }
        }
        client.stop();
        if (g_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

static bool pop_frame(Frame &f) {
    std::unique_lock<std::mutex> lk(g_q_mtx);
    g_q_cv.wait(lk, [] { return !g_queue.empty() || !g_running.load(); });
    if (g_queue.empty()) return false;
    f = std::move(g_queue.front());
    g_queue.pop_front();
    return true;
}

int main() {
    if (!CAM_PASSWORD || !*CAM_PASSWORD || !CAM_PASS_MD5 || !*CAM_PASS_MD5) {
        fprintf(stderr, "Camera credentials missing: load camera.env before starting.\n");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0);
    xt::Json settings;
    try {
        settings = xt::load_config(true);
        xt::configure_protocol_clock(settings.value("timestamp_mode", "monotonic"));
        TARGET_HZ = settings["sensors"]["image_fps"].get<int>();
        // config.json stores the ImageMsg.format space (3=H264, 2=JPEG);
        // legacy configs still carrying 1 (H264) are honoured as well.
        const int fmt = settings["sensors"]["image_format"].get<int>();
        IMAGE_MODE = (fmt == 1 || fmt == 3) ? IMAGE_FORMAT_H264 : IMAGE_FORMAT_JPEG;
        H264_GOP = settings["sensors"]["h264_gop"].get<int>();
        H264_BITRATE_BPS = settings["sensors"]["h264_bitrate"].get<int>() * 1024 * 1024;
        H264_NATIVE_PASSTHROUGH = settings["sensors"].value("h264_native_passthrough", false);
    }
    catch (const std::exception &e) { fprintf(stderr, "[cfg] %s\n", e.what()); return 1; }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 1;
    const bool h264_mode = IMAGE_MODE == IMAGE_FORMAT_H264;
    const bool jpeg_transcode = !h264_mode && jpeg_transcode_enabled(settings["sensors"]);
    if ((h264_mode || jpeg_transcode) && !configure_native_h264(settings["sensors"])) {
        fprintf(stderr, "[h264] refusing to publish because native encoder configuration failed\n");
        return 1;
    }
    printf("[cam] MC800S native %s -> protocol image channel format=%d\n",
           h264_mode ? "H264 RTSP" : jpeg_transcode ? "JPEG transcode (H264 RTSP -> MJPEG)" : "JPEG snapshot",
           IMAGE_MODE);
    g_raw_enabled = env_enabled("XT_CAMERA_RAW");
    printf("[cam] raw record %s dir=%s/ window=%ds\n", g_raw_enabled ? "on" : "off", RAW_DIR, RAW_WINDOW_SEC);
    load_sn();
    if (g_raw_enabled) {
        raw_mkdir();
        raw_cleanup(time(NULL));
    }

    printf("[cam] image handoff=%s; sensor-neutral xt_gateway owns the sole Zenoh session\n",
           IMAGE_INGEST_SOCKET);

    std::thread cap(h264_mode ? capture_h264_thread_main :
                    jpeg_transcode ? capture_jpeg_transcode_thread_main : capture_jpeg_thread_main);
    uint64_t published = 0, dropped = 0, put_failures = 0;
    uint64_t last_capture_id = 0;

    while (true) {
        Frame f;
        if (!pop_frame(f)) continue;
        if (last_capture_id && f.capture_id > last_capture_id + 1) dropped += f.capture_id - last_capture_id - 1;
        last_capture_id = f.capture_id;
        double queue_ms = (monotonic_ns() - f.fetched_ns) / 1e6;
        if (queue_ms > 300) { ++dropped; continue; }
        size_t rw = raw_append(f.stamp_us, f.data.data(), f.data.size());

        uint32_t seq = g_seq++;
        auto encode_start = monotonic_ns();
        auto msg = build_image_array_msg(f, seq);
        double pb_ms = (monotonic_ns() - encode_start)/1e6;
        auto put_start = monotonic_ns();
        int pr = send_image_payload(msg);
        double put_ms = (monotonic_ns() - put_start)/1e6;
        if (pr != Z_OK) ++put_failures;
        published++;
        if ((published % 100) == 0 || pr != Z_OK || put_ms > 100 || f.http_ms > 200) {
            printf("[cam] seq=%u pub=%llu format=%u data=%zuB %ux%u msg=%zuB put=%d dropped=%llu raw%zuB source_ms=%.2f queue_ms=%.2f pb_ms=%.2f put_ms=%.2f put_fail=%llu\n",
                   seq, (unsigned long long)published, f.format, f.data.size(), f.width, f.height,
                   msg.size(), pr, (unsigned long long)dropped, rw, f.http_ms, queue_ms, pb_ms, put_ms,
                   (unsigned long long)put_failures);
        }
    }

    g_running = false;
    g_q_cv.notify_all();
    cap.join();
    return 0;
}
