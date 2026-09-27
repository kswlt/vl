/******************************************************************************
 * xt_radar.cpp — XT-M60 固态激光雷达自研驱动 + 空间相机通信协议V1.0 协议转换
 *
 * 雷达接口 (ToFFuture XT-M60 手册):
 *   TCP 7787: 指令通道 (组包: 7E FF AA 55 | size(LE) | cmdid | data | 00 01 | FF 7E 55 AA)
 *   UDP 7687: 点云/IMU 数据 (20字节包头 + 1400字节负载, 按帧号组帧)
 *
 * 数据通道 (Zenoh pub/sub, Protobuf payload):
 *   active/{sn}/pointcloud   LidarPointMsgArray  5Hz  scaler=1000 (毫米)
 *   pointcloud_preview is debug-only and disabled by default; it is not a V1.0 topic
 *   active/{sn}/imu          not published: no usable hardware IMU source
 * 指令通道 (Zenoh queryable):
 *   active/{sn}/cmd/setting  /  active/{sn}/cmd/reboot
 ******************************************************************************/
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <array>
#include <algorithm>
#include <csignal>
#include <mutex>
#include <fstream>
#include <sstream>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ifaddrs.h>
#include <spawn.h>
#include <sys/wait.h>

#include "zenoh-pico.h"
#include "zenoh_config.hpp"
#include "zenoh_qos.hpp"
#include "protocol_clock.hpp"
#include "device_sn.hpp"
#include "request_parser.hpp"
#include "publish_queue.hpp"

// ==================== 配置 ====================
static const char *RADAR_IP = "192.0.2.101";
static const int TCP_PORT = 7787;
static const int UDP_PORT = 7687;
static const char *BOARD_IP = "192.0.2.179"; // 板子 eth0(雷达直连)辅助地址; 雷达 UDP 目标
static const char *RADAR_IF = "eth0";          // 雷达所在网口(多播组加入接口)
static std::string g_radar_source, g_radar_interface;
static const char *SN_FILE = "/mnt/system/factory-data/Legacy.yaml";
static std::string g_sn;
static double PC_HZ = 5.0;
static const int64_t PC_SCALER = 1000;
static const size_t PC_PREVIEW_MAX_POINTS = 96;
static const char *PC_STREAM_IP = "192.0.2.200"; // PC 经路由器(192.0.2.x)接收全量点云
static const int PC_STREAM_PORT = 17778;
static const size_t PC_STREAM_CHUNK = 1200;
static const uint16_t CUT_CORNER = 60; // 四角切除(与SDK示例一致)

// ==================== 全局 ====================
static z_owned_session_t g_zs;
static z_owned_publisher_t g_pub_pc;
static z_owned_publisher_t g_pub_img;
static z_owned_publisher_t g_pub_pc_preview;
static bool g_preview_enabled = false;
static z_owned_queryable_t g_qable;
static int g_pc_stream_fd = -1;
static struct sockaddr_in g_pc_stream_addr;
static std::atomic<bool> g_zenoh_ok{false};
static std::atomic<uint32_t> g_pc_seq{0};
static std::atomic<uint32_t> g_cmd_seq{0}, g_file_seq{0};
static xt::Json g_config, g_active_config;
static std::mutex g_config_mtx;
static z_owned_publisher_t g_pub_files;
static z_owned_queryable_t g_file_queryable;
static std::atomic<bool> g_files_requested{true};
static std::mutex g_files_mtx;
static std::vector<uint8_t> g_files_payload;
static std::atomic<uint64_t> g_last_pc_ms{0};
static const char *IMAGE_INGEST_SOCKET = "/tmp/xt_image_ingest.sock";
static const char *POINTCLOUD_INGEST_SOCKET = "/tmp/xt_pointcloud_ingest.sock";
static bool g_gateway_mode = false;
static bool g_ingest_only = false;
static int g_point_ingest_fd = -1;

static const char *PLACEHOLDER_CALIBRATION = R"json({
  "calibration_status": "placeholder",
  "valid": false,
  "camera": {
    "model": "MC800S",
    "width": 1920,
    "height": 1080,
    "fx": 1000.0,
    "fy": 1000.0,
    "cx": 960.0,
    "cy": 540.0,
    "distortion": {"k1": 0.0, "k2": 0.0, "p1": 0.0, "p2": 0.0, "k3": 0.0}
  },
  "lidar_to_camera": {"R": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]], "t": [0.0, 0.0, 0.0]},
  "note": "Placeholder only; replace with physical MC800S RGB calibration. XT-M60 cmd18 is not camera calibration."
})json";

// 雷达相机内参 (M60 默认, 优先从 cmd18 获取)
static float g_fx = 78.212524f, g_fy = 79.055527f, g_cx = 80.701973f, g_cy = 26.804274f;
static float g_k1 = -0.302470f, g_k2 = 0.074865f, g_k3 = 0.0f, g_p1 = -0.002307f, g_p2 = -0.001400f;

// 当前配置 (setting 应答用)

// ==================== 工具 ====================
static uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n\"");
    size_t b = s.find_last_not_of(" \t\r\n\"");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

static bool load_sn() {
    const bool found = xt::load_existing_device_sn(SN_FILE, g_sn);
    if (!found) g_sn.clear();
    printf("[cfg] existing device_sn=%s\n", found ? g_sn.c_str() : "<none>");
    return found;
}

static bool extract_radar_sn(const std::vector<uint8_t> &response, std::string &sn) {
    // cmd4 data is DevInfo_t: magic(4), size(2), version(1), sn[28].
    if (response.size() < 35 || memcmp(response.data(), "\x51\xaa\xcc\x33", 4) != 0) return false;
    char raw[29] = {};
    memcpy(raw, response.data() + 7, 28);
    sn = trim(raw);
    return xt::valid_device_sn(sn);
}


// ==================== Protobuf 编码器 ====================
class PBWriter {
public:
    std::vector<uint8_t> buf;
    void varint(uint64_t v) {
        while (v >= 0x80) { buf.push_back((uint8_t)(v & 0x7f) | 0x80); v >>= 7; }
        buf.push_back((uint8_t)v);
    }
    void tag(int field, int wire) { varint(((uint64_t)field << 3) | (uint64_t)wire); }
    void u32(int field, uint32_t v) { tag(field, 0); varint(v); }
    void s32(int field, int32_t v) { tag(field, 0); varint(zigzag(v)); }
    void s64(int field, int64_t v) { tag(field, 0); varint(zigzag(v)); }
    void len(int field, const uint8_t *p, size_t n) { tag(field, 2); varint(n); buf.insert(buf.end(), p, p + n); }
    void len(int field, const std::vector<uint8_t> &m) { len(field, m.data(), m.size()); }
    void str(int field, const std::string &s) { len(field, (const uint8_t *)s.data(), s.size()); }
    static uint64_t zigzag(int64_t v) { return (uint64_t(v) << 1) ^ uint64_t(-(v < 0)); }
};

static void enc_header(PBWriter &w, uint32_t seq, int64_t stamp, int64_t scaler) {
    PBWriter h;
    h.u32(1, seq);
    h.s64(2, stamp);
    h.s64(3, scaler);
    w.len(1, h.buf);
}

static void enc_error(PBWriter &w, uint32_t code, const std::string &desc) {
    PBWriter e;
    e.u32(1, code);
    e.str(2, desc);
    w.len(15, e.buf);
}

// LidarPoint { sint64 x=1; y=2; z=3; uint32 rgbi=4; uint32 ring=5; sint32 offset=6 }
// 协议: LidarPointMsg { Header header=1; repeated LidarPoint points=2 }
static void enc_point(PBWriter &w, int64_t x, int64_t y, int64_t z, uint32_t rgbi, uint32_t ring, int32_t offset) {
    PBWriter pt; // LidarPoint
    pt.s64(1, x); pt.s64(2, y); pt.s64(3, z);
    pt.u32(4, rgbi); pt.u32(5, ring); pt.s32(6, offset);
    w.len(2, pt.buf); // LidarPointMsg.points 字段2 (嵌套)
}

static std::vector<uint8_t> build_pointcloud_msg(
    const std::vector<float> &px, const std::vector<float> &py, const std::vector<float> &pz,
    const std::vector<uint8_t> &inten, int64_t stamp_us, uint32_t seq) {
    PBWriter arr;
    {
        PBWriter msg;
        enc_header(msg, seq, stamp_us, PC_SCALER);
        for (size_t i = 0; i < px.size(); i++) {
            if (std::isnan(px[i]) || std::isnan(py[i]) || std::isnan(pz[i])) continue;
            int64_t x = (int64_t)llround((double)px[i] * 1000.0);
            int64_t y = (int64_t)llround((double)py[i] * 1000.0);
            int64_t z = (int64_t)llround((double)pz[i] * 1000.0);
            enc_point(msg, x, y, z, inten[i], 0, 0);
        }
        arr.len(1, msg.buf);
    }
    return arr.buf;
}

static std::vector<uint8_t> build_pointcloud_preview_msg(
    const std::vector<float> &px, const std::vector<float> &py, const std::vector<float> &pz,
    const std::vector<uint8_t> &inten, int64_t stamp_us, uint32_t seq, size_t valid_count) {
    size_t stride = 1;
    if (valid_count > PC_PREVIEW_MAX_POINTS) {
        stride = (valid_count + PC_PREVIEW_MAX_POINTS - 1) / PC_PREVIEW_MAX_POINTS;
    }

    PBWriter arr;
    PBWriter msg;
    enc_header(msg, seq, stamp_us, PC_SCALER);
    size_t seen = 0;
    size_t emitted = 0;
    for (size_t i = 0; i < px.size() && emitted < PC_PREVIEW_MAX_POINTS; i++) {
        if (std::isnan(px[i]) || std::isnan(py[i]) || std::isnan(pz[i])) continue;
        if ((seen++ % stride) != 0) continue;
        int64_t x = (int64_t)llround((double)px[i] * 1000.0);
        int64_t y = (int64_t)llround((double)py[i] * 1000.0);
        int64_t z = (int64_t)llround((double)pz[i] * 1000.0);
        enc_point(msg, x, y, z, inten[i], 0, 0);
        emitted++;
    }
    arr.len(1, msg.buf);
    return arr.buf;
}

static void pc_stream_init() {
    const char *enabled = getenv("XT_PC_UDP_MIRROR");
    if (!enabled || strcmp(enabled, "1") != 0) return;
    g_pc_stream_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_pc_stream_fd < 0) {
        printf("[pcstream] UDP socket 创建失败 errno=%d\n", errno);
        return;
    }
    memset(&g_pc_stream_addr, 0, sizeof(g_pc_stream_addr));
    g_pc_stream_addr.sin_family = AF_INET;
    g_pc_stream_addr.sin_port = htons(PC_STREAM_PORT);
    if (inet_pton(AF_INET, PC_STREAM_IP, &g_pc_stream_addr.sin_addr) != 1) {
        printf("[pcstream] PC IP 无效: %s\n", PC_STREAM_IP);
        close(g_pc_stream_fd);
        g_pc_stream_fd = -1;
        return;
    }
    printf("[pcstream] 全量点云 UDP 分片目标 %s:%d chunk=%zu\n", PC_STREAM_IP, PC_STREAM_PORT, PC_STREAM_CHUNK);
}

static void pc_stream_send(uint32_t seq, const std::vector<uint8_t> &msg) {
    if (g_pc_stream_fd < 0 || msg.empty()) return;
    uint32_t total_len = (uint32_t)msg.size();
    uint32_t chunk_count = (total_len + (uint32_t)PC_STREAM_CHUNK - 1) / (uint32_t)PC_STREAM_CHUNK;
    uint8_t pkt[24 + PC_STREAM_CHUNK];
    uint32_t magic = 0x43505458; // "XTPC" little-endian
    uint32_t sent = 0;
    for (uint32_t idx = 0; idx < chunk_count; idx++) {
        uint32_t off = idx * (uint32_t)PC_STREAM_CHUNK;
        uint32_t clen = total_len - off;
        if (clen > (uint32_t)PC_STREAM_CHUNK) clen = (uint32_t)PC_STREAM_CHUNK;
        memcpy(pkt + 0, &magic, 4);
        memcpy(pkt + 4, &seq, 4);
        memcpy(pkt + 8, &total_len, 4);
        memcpy(pkt + 12, &idx, 4);
        memcpy(pkt + 16, &chunk_count, 4);
        memcpy(pkt + 20, &clen, 4);
        memcpy(pkt + 24, msg.data() + off, clen);
        ssize_t n = sendto(g_pc_stream_fd, pkt, 24 + clen, 0,
                           (struct sockaddr *)&g_pc_stream_addr, sizeof(g_pc_stream_addr));
        if (n == (ssize_t)(24 + clen)) sent++;
    }
    if (seq % 10 == 0 || sent != chunk_count) {
        printf("[pcstream] seq=%u full_bytes=%u chunks=%u sent=%u\n", seq, total_len, chunk_count, sent);
    }
}


// ==================== 雷达 TCP 指令通道 ====================
static int g_tcpfd = -1;
static std::mutex g_tcp_mtx;
static std::vector<uint8_t> g_control_rx;
static bool g_debug = false;

static bool wait_socket(int fd, bool writing, uint64_t deadline) {
    while (true) {
        uint64_t now=now_ms();
        if (now >= deadline) return false;
        uint64_t left = deadline - now;
        timeval tv{long(left/1000), long(left%1000)*1000};
        fd_set set; FD_ZERO(&set); FD_SET(fd,&set);
        int r = select(fd+1, writing ? NULL : &set, writing ? &set : NULL, NULL, &tv);
        if (r < 0 && errno == EINTR) continue;
        return r > 0;
    }
    return false;
}
static bool socket_io(int fd, uint8_t *data, size_t size, bool writing, uint64_t deadline) {
    size_t off=0;
    while (off<size) {
        if (!wait_socket(fd,writing,deadline)) return false;
        ssize_t n = writing ? send(fd,data+off,size-off,MSG_NOSIGNAL) : recv(fd,data+off,size-off,0);
        if (n<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK)) continue;
        if (n<=0) return false;
        off += size_t(n);
    }
    return true;
}
static bool run_ip(std::vector<const char *> args) {
    extern char **environ;
    std::vector<char *> argv;
    for (const char *arg : args) argv.push_back(const_cast<char *>(arg));
    argv.push_back(nullptr);
    pid_t child;
    if (posix_spawnp(&child, "ip", nullptr, nullptr, argv.data(), environ) != 0) return false;
    uint64_t deadline=now_ms()+2000;
    int status=0;
    while (now_ms()<deadline) {
        pid_t result=waitpid(child,&status,WNOHANG);
        if (result==child) return WIFEXITED(status) && WEXITSTATUS(status)==0;
        if (result<0 && errno!=EINTR) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    kill(child,SIGKILL);
    while (waitpid(child,&status,0)<0 && errno==EINTR) {}
    return false;
}
static bool radar_route_present(std::istream &routes) {
    std::string line;
    while (std::getline(routes,line)) {
        char iface[64];
        unsigned destination,gateway,flags,mask;
        if (sscanf(line.c_str(),"%63s %x %x %x %*u %*u %*u %x",
                   iface,&destination,&gateway,&flags,&mask)==5 &&
            strcmp(iface,RADAR_IF)==0 && destination==inet_addr(RADAR_IP) &&
            gateway==0 && mask==0xffffffffU && (flags&1)) return true;
    }
    return false;
}
static bool ensure_radar_network() {
    bool present=false;
    ifaddrs *addresses=nullptr;
    if (getifaddrs(&addresses)!=0) return false;
    for (auto *a=addresses; a; a=a->ifa_next)
        if (a->ifa_addr && a->ifa_addr->sa_family==AF_INET && strcmp(a->ifa_name,RADAR_IF)==0 &&
            reinterpret_cast<sockaddr_in *>(a->ifa_addr)->sin_addr.s_addr==inet_addr(BOARD_IP)) present=true;
    freeifaddrs(addresses);
    const std::string address=std::string(BOARD_IP)+"/32", route=std::string(RADAR_IP)+"/32";
    if (!present && !run_ip({"ip","addr","add",address.c_str(),"dev",RADAR_IF})) return false;
    std::ifstream routes("/proc/net/route");
    if (radar_route_present(routes)) return true;
    // Linux removes this host route when eth0 goes down. Leave existing correct routes alone.
    printf("[recovery] restoring radar host route via %s\n",RADAR_IF);
    return run_ip({"ip","route","replace",route.c_str(),"dev",RADAR_IF,"src",BOARD_IP});
}
static bool tcp_connect_radar() {
    std::lock_guard<std::mutex> lk(g_tcp_mtx);
    if (g_tcpfd >= 0) close(g_tcpfd);
    g_tcpfd=-1;
    g_control_rx.clear();
    if (!ensure_radar_network()) { printf("[recovery] radar address/route unavailable\n"); return false; }
    int fd=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if (fd<0) return false;
    if (setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,RADAR_IF,strlen(RADAR_IF)+1)!=0) { close(fd); return false; }
    sockaddr_in src{};
    src.sin_family=AF_INET; src.sin_addr.s_addr=inet_addr(BOARD_IP);
    if (bind(fd,(sockaddr *)&src,sizeof(src))!=0) { close(fd); return false; }
    sockaddr_in dst{};
    dst.sin_family=AF_INET; dst.sin_port=htons(TCP_PORT); dst.sin_addr.s_addr=inet_addr(RADAR_IP);
    int r=connect(fd,(sockaddr *)&dst,sizeof dst);
    if (r != 0 && (errno != EINPROGRESS || !wait_socket(fd,true,now_ms()+1500))) { close(fd); return false; }
    int error=0; socklen_t len=sizeof error;
    if (getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&len)!=0 || error) { close(fd); return false; }
    g_tcpfd=fd; return true;
}
static bool decode_radar_response(const uint8_t *body, size_t size,
                                  std::vector<uint8_t> &data, uint8_t &status) {
    data.clear();
    if (size<3) return false;
    uint8_t version=body[size-1];
    size_t trailer=version==3 ? 3 : 2;
    if ((version!=1 && version!=2 && version!=3) || size<trailer+1) return false;
    status=version==3 ? body[size-3] : body[size-2]&0x0f;
    data.assign(body+1,body+size-trailer);
    return true;
}
static void log_radar_event(const uint8_t *body, size_t size) {
    std::vector<uint8_t> data;
    uint8_t status=0xff;
    if (!decode_radar_response(body,size,data,status)) return;
    printf("[sensor-event] cmd=%u status=%u data=",unsigned(body[0]),unsigned(status));
    for (size_t i=0;i<std::min(data.size(),size_t(32));++i) printf("%02x",unsigned(data[i]));
    printf(" bytes=%zu\n",data.size());
}
static void drain_control_events() {
    std::lock_guard<std::mutex> lk(g_tcp_mtx);
    if (g_tcpfd<0) return;
    auto fail=[&] { close(g_tcpfd); g_tcpfd=-1; g_control_rx.clear(); };
    uint8_t chunk[4096];
    for (int reads=0;reads<16;++reads) {
        ssize_t n=recv(g_tcpfd,chunk,sizeof chunk,MSG_DONTWAIT);
        if (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) break;
        if (n<0 && errno==EINTR) continue;
        if (n<=0 || g_control_rx.size()+size_t(n)>65536) { fail(); return; }
        g_control_rx.insert(g_control_rx.end(),chunk,chunk+n);
        while (g_control_rx.size()>=8) {
            const auto *p=g_control_rx.data();
            uint32_t size=uint32_t(p[4]) | uint32_t(p[5])<<8 | uint32_t(p[6])<<16 | uint32_t(p[7])<<24;
            if (memcmp(p,"\x7e\xff\xaa\x55",4)!=0 || size<3 || size>65524) { fail(); return; }
            if (g_control_rx.size()<size+12) break;
            if (memcmp(p+8+size,"\xff\x7e\x55\xaa",4)!=0) { fail(); return; }
            log_radar_event(p+8,size);
            g_control_rx.erase(g_control_rx.begin(),g_control_rx.begin()+size+12);
        }
    }
}
static bool radar_cmd(uint8_t cmdid, const std::vector<uint8_t> &data, std::vector<uint8_t> &data_out, int timeout_ms=1500) {
    std::lock_guard<std::mutex> lk(g_tcp_mtx);
    data_out.clear();
    if (g_tcpfd < 0 || data.size() > 4090) return false;
    auto fail = [&] { close(g_tcpfd); g_tcpfd=-1; return false; };
    uint32_t size = uint32_t(data.size())+3;
    std::vector<uint8_t> pkt{0x7e,0xff,0xaa,0x55,uint8_t(size),uint8_t(size>>8),uint8_t(size>>16),uint8_t(size>>24),cmdid};
    pkt.insert(pkt.end(),data.begin(),data.end());
    pkt.insert(pkt.end(),{0,1,0xff,0x7e,0x55,0xaa});
    const uint64_t deadline=now_ms()+timeout_ms;
    if (!socket_io(g_tcpfd,pkt.data(),pkt.size(),true,deadline)) return fail();
    for (int attempt=0; attempt<6; ++attempt) {
        uint8_t header[8];
        if (!socket_io(g_tcpfd,header,8,false,deadline) || memcmp(header,"\x7e\xff\xaa\x55",4)!=0) return fail();
        size = uint32_t(header[4]) | uint32_t(header[5])<<8 | uint32_t(header[6])<<16 | uint32_t(header[7])<<24;
        if (size<3 || size>4096) return fail();
        std::vector<uint8_t> body(size+4);
        if (!socket_io(g_tcpfd,body.data(),body.size(),false,deadline) ||
            memcmp(body.data()+size,"\xff\x7e\x55\xaa",4)!=0) return fail();
        if (body[0] == cmdid) {
            uint8_t status=0xff;
            if (!decode_radar_response(body.data(),size,data_out,status)) return fail();
            if (status==0) return true;
            printf("[xt] command=%u rejected status=%u\n",unsigned(cmdid),unsigned(status));
            data_out.clear();
            return false;
        }
        log_radar_event(body.data(),size);
    }
    return fail();
}

static std::vector<uint8_t> radar_udp_destination() {
    uint32_t ip=ntohl(inet_addr(BOARD_IP));
    // cmd19 uses the device's little-endian protocol, not network byte order, for the port.
    return {uint8_t(ip>>24),uint8_t(ip>>16),uint8_t(ip>>8),uint8_t(ip),
            uint8_t(UDP_PORT),uint8_t(UDP_PORT>>8)};
}
static bool configure_radar_udp() {
    std::vector<uint8_t> response;
    if (!radar_cmd(19,radar_udp_destination(),response)) return false;
    if (radar_cmd(4,{},response) && response.size()>=99 &&
        memcmp(response.data(),"\x51\xaa\xcc\x33",4)==0) {
        unsigned port=unsigned(response[97]) | unsigned(response[98])<<8;
        printf("[xt] UDP target readback=%u.%u.%u.%u:%u\n",unsigned(response[93]),
               unsigned(response[94]),unsigned(response[95]),unsigned(response[96]),port);
        if (port!=UDP_PORT) return false;
    }
    return true;
}

// ==================== 雷达 UDP 数据接收/组帧 ====================
struct FrameBuf {
    uint16_t sn = 0;
    uint32_t size = 0, count = 0;
    bool used = false;
    std::vector<uint8_t> data;
    std::vector<uint8_t> covered;
    uint64_t born_ms = 0;
};

static FrameBuf g_frames[3];
static const int UDP_HEADER = 20;

static void assemble_udp(const uint8_t *p, size_t len, std::vector<uint8_t> &frame_out, bool &is_frame, bool &is_imu, std::vector<uint8_t> &imu_out) {
    is_frame = is_imu = false;
    if (len < UDP_HEADER + 8) return;
    uint16_t imagesn = (uint16_t)(p[0] | (p[1] << 8));
    uint32_t totalsize = (uint32_t)p[2] | ((uint32_t)p[3] << 8) | ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 24);
    uint16_t payloadSize = (uint16_t)(p[6] | (p[7] << 8));
    uint32_t sentsize = (uint32_t)p[8] | ((uint32_t)p[9] << 8) | ((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);

    if (!payloadSize || payloadSize > 1400 || payloadSize + UDP_HEADER > len) return;

    // IMU 帧: 小包且负载内带 cmdid 252
    if (payloadSize >= 13 && payloadSize < 200 && p[UDP_HEADER + 8] == 252) {
        uint32_t sm = (uint32_t)p[UDP_HEADER] << 24 | (uint32_t)p[UDP_HEADER + 1] << 16 | (uint32_t)p[UDP_HEADER + 2] << 8 | p[UDP_HEADER + 3];
        uint32_t em = (uint32_t)p[UDP_HEADER + payloadSize - 4] << 24 | (uint32_t)p[UDP_HEADER + payloadSize - 3] << 16 | (uint32_t)p[UDP_HEADER + payloadSize - 2] << 8 | p[UDP_HEADER + payloadSize - 1];
        if (sm == 0x7EFFAA55 && em == 0xFF7E55AA) {
            imu_out.assign(p + UDP_HEADER + 8, p + UDP_HEADER + payloadSize - 4);
            is_imu = true;
            return;
        }
    }

    // 普通帧组帧
    if (totalsize < 13 || totalsize > 1200000 || sentsize > totalsize || payloadSize > totalsize-sentsize) return;
    for (auto &f : g_frames) if (f.used && now_ms()-f.born_ms > 1000) f.used=false;
    int idx = -1;
    for (int i = 0; i < 3; i++) if (g_frames[i].used && g_frames[i].sn == imagesn) { idx = i; break; }
    if (idx < 0) {
        for (int i = 0; i < 3; i++) if (!g_frames[i].used) { idx = i; break; }
        if (idx < 0) {
            idx=0;
            for (int i=1; i<3; ++i) if (g_frames[i].born_ms < g_frames[idx].born_ms) idx=i;
        }
        g_frames[idx].used = true;
        g_frames[idx].sn = imagesn;
        g_frames[idx].size = totalsize;
        g_frames[idx].count = 0;
        g_frames[idx].data.assign(totalsize, 0);
        g_frames[idx].covered.assign(totalsize, 0);
        g_frames[idx].born_ms = now_ms();
    }
    if (totalsize != g_frames[idx].size) return;
    for (uint32_t pos=sentsize; pos<sentsize+payloadSize; ++pos) {
        if (g_frames[idx].covered[pos]) {
            if (g_frames[idx].data[pos] != p[UDP_HEADER+pos-sentsize]) { g_frames[idx].used=false; return; }
        } else {
            g_frames[idx].covered[pos]=1;
            ++g_frames[idx].count;
        }
    }
    memcpy(g_frames[idx].data.data() + sentsize, p + UDP_HEADER, payloadSize);
    if (g_frames[idx].count >= g_frames[idx].size) {
        g_frames[idx].used = false;
        if (g_frames[idx].count == g_frames[idx].size) {
            uint32_t sm = (uint32_t)g_frames[idx].data[0] << 24 | (uint32_t)g_frames[idx].data[1] << 16 | (uint32_t)g_frames[idx].data[2] << 8 | g_frames[idx].data[3];
            uint32_t em = (uint32_t)g_frames[idx].data[totalsize - 4] << 24 | (uint32_t)g_frames[idx].data[totalsize - 3] << 16 | (uint32_t)g_frames[idx].data[totalsize - 2] << 8 | g_frames[idx].data[totalsize - 1];
            if (sm == 0x7EFFAA55 && em == 0xFF7E55AA) {
                frame_out.assign(g_frames[idx].data.begin() + 8, g_frames[idx].data.begin() + totalsize - 4);
                is_frame = true;
            }
        }
    }
}

// ==================== 帧解析 (移植 SDK frame.cpp) ====================
struct FrameInfo {
    uint32_t magicToken;
    uint8_t endianType;
    uint8_t sn[29];
    uint8_t fw[19];
    uint8_t lensparameters[40];
    uint8_t imageflags;
    uint8_t hdrmode;
    uint8_t levelused_bit;
    uint8_t freq[5];
    uint16_t integtime[5];
    uint16_t integtimegs;
    uint16_t roix[2];
    uint16_t roiy[2];
    uint16_t miniAmp;
    uint8_t channel, binning, reduce, vcsel, fps, aldcslevel, is2dcsFlag, reserver1[6];
    uint8_t unit_div;
    uint16_t width, height;
    int16_t temperature[2];
    uint8_t timesync_type, timesync_state;
    uint64_t timestamp[5];
    int32_t ptpoffsetus;
    uint8_t imusize, otherflags, chiptype, reserver2[3], version;
    uint32_t crc32;
    uint16_t infosize;
    uint8_t devstate, ptclversion;
} __attribute__((packed));

static bool parse_frame(const std::vector<uint8_t> &fd, uint16_t &width, uint16_t &height,
                        std::vector<uint32_t> &dist, std::vector<uint16_t> &amp,
                        uint64_t &stamp_s, uint32_t &stamp_ns, FrameInfo &info_out) {
    if (fd.size() < 64) return false;
    uint8_t cmdid = fd[0];
    if (cmdid != 251) return false;
    uint16_t fw = (uint16_t)(fd[2] | (fd[3] << 8));
    (void)fw;
    width = (uint16_t)(fd[4] | (fd[5] << 8));
    height = (uint16_t)(fd[6] | (fd[7] << 8));
    if (width < 1 || width > 160 || height < 1 || height > 60) return false;

    uint32_t pkg = (uint32_t)fd.size();
    uint16_t infosize = (uint16_t)(fd[pkg - 4] | (fd[pkg - 3] << 8));
    if (infosize != sizeof(FrameInfo) || infosize > pkg-16) return false;
    int infopos = (int)pkg - (int)infosize;
    FrameInfo info{};
    memcpy(&info, fd.data() + infopos, infosize);
    if (info.magicToken != 0x33CCAA50 || info.roix[0]+width > 160 || info.roiy[0]+height > 60) return false;
    info_out = info;

    uint8_t imageflags = info.imageflags;
    uint32_t pixelcount = (uint32_t)width * height;
    int pixelDataOffset = 16;

    uint32_t unitmm = info.unit_div ? info.unit_div : 1;
    stamp_s = info.timestamp[0] / 1000;
    stamp_ns = (uint32_t)(info.timestamp[0] % 1000) * 1000000;

    dist.assign(pixelcount, 0);
    amp.assign(pixelcount, 0);

    if (imageflags & 0x01) { // IMG_DIST
        int pixelbytes = (imageflags & 0x02) ? 4 : 2;
        for (uint32_t i = 0; i < pixelcount; i++) {
            int off = pixelDataOffset + (int)i * pixelbytes;
            if (off + 1 >= infopos) return false;
            uint32_t d = (uint32_t)(fd[off + 1] << 8 | fd[off]);
            if (d < 64000) { if (unitmm > 1) d *= unitmm; }
            else d += 900000;
            dist[i] = d;
        }
    }
    if (imageflags & 0x02) { // IMG_AMP
        int pixelbytes = (imageflags & 0x01) ? 4 : 2;
        int abase = (imageflags & 0x01) ? 2 : 0;
        for (uint32_t i = 0; i < pixelcount; i++) {
            int off = pixelDataOffset + (int)i * pixelbytes + abase;
            if (off + 1 >= infopos) return false;
            amp[i] = (uint16_t)(fd[off + 1] << 8 | fd[off]);
        }
    }
    return true;
}

// ==================== 点云计算 (移植 SDK cartesianTransform.cpp) ====================
static std::vector<float> g_mapx, g_mapy; // 去畸变后的归一化坐标 (x_n, y_n)
static std::vector<bool> g_mapdel;
static int g_sensor_w = 160, g_sensor_h = 60;

static void build_maptable() {
    g_sensor_w = 160;
    g_sensor_h = 60;
    int n = g_sensor_w * g_sensor_h;
    g_mapx.assign(n, 0.0f);
    g_mapy.assign(n, 0.0f);
    g_mapdel.assign(n, false);
    for (int h = 0; h < g_sensor_h; h++) {
        for (int w = 0; w < g_sensor_w; w++) {
            int idx = h * g_sensor_w + w;
            float xd = ((float)w - g_cx) / g_fx;
            float yd = ((float)h - g_cy) / g_fy;
            float x = xd, y = yd;
            for (int it = 0; it < 6; it++) { // 迭代求逆
                float r2 = x * x + y * y;
                float r4 = r2 * r2;
                float r6 = r4 * r2;
                float k = 1.0f + g_k1 * r2 + g_k2 * r4 + g_k3 * r6;
                x = (xd - (2 * g_p1 * x * y + g_p2 * (r2 + 2 * x * x))) / k;
                y = (yd - (2 * g_p2 * x * y + g_p1 * (r2 + 2 * y * y))) / k;
            }
            g_mapx[idx] = x;
            g_mapy[idx] = y;
        }
    }
    // 四角切除 (chamfer)
    int chamfer = CUT_CORNER > 0 ? (int)CUT_CORNER / 4 : 0;
    if (chamfer > 0 && chamfer < 201) {
        for (int h = 0; h < g_sensor_h; h++) {
            for (int w = 0; w < g_sensor_w; w++) {
                bool del = false;
                if (w < chamfer) {
                    if (h < chamfer && sqrt((chamfer - w) * (chamfer - w) + (chamfer - h) * (chamfer - h)) > chamfer) del = true;
                    if (h > g_sensor_h - chamfer && sqrt((chamfer - w) * (chamfer - w) + (h - (g_sensor_h - chamfer)) * (h - (g_sensor_h - chamfer))) > chamfer) del = true;
                }
                if (w > g_sensor_w - chamfer) {
                    if (h < chamfer && sqrt((w - (g_sensor_w - chamfer)) * (w - (g_sensor_w - chamfer)) + (chamfer - h) * (chamfer - h)) > chamfer) del = true;
                    if (h > g_sensor_h - chamfer && sqrt((w - (g_sensor_w - chamfer)) * (w - (g_sensor_w - chamfer)) + (h - (g_sensor_h - chamfer)) * (h - (g_sensor_h - chamfer))) > chamfer) del = true;
                }
                g_mapdel[h * g_sensor_w + w] = del;
            }
        }
    }
}

static void compute_points(const std::vector<uint32_t> &dist, const std::vector<uint16_t> &amp,
                           uint16_t width, uint16_t height, const FrameInfo &info,
                           std::vector<float> &px, std::vector<float> &py, std::vector<float> &pz,
                           std::vector<uint8_t> &inten) {
    px.clear(); py.clear(); pz.clear(); inten.clear();
    uint32_t n = (uint32_t)width * height;
    px.reserve(n); py.reserve(n); pz.reserve(n); inten.reserve(n);
    // M60 反射率系数 (refcof_M60[0] 默认频率)
    float refcoef = 0.029626f;
    uint32_t integ = info.integtime[0];
    float binning_div = 1.0f;
    if (info.binning & 0x01) binning_div *= 2.0f;
    if (info.binning & 0x02) binning_div *= 2.0f;
    refcoef /= binning_div;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t curdis = dist[i];
        uint32_t p = i / width, q = i % width;
        int index = (int)(p * 1 + info.roiy[0]) * g_sensor_w + (int)(q * 1 + info.roix[0]);
        if (index >= g_sensor_w * g_sensor_h) index = g_sensor_w * g_sensor_h - 1;
        index = (index / g_sensor_w) * g_sensor_w + g_sensor_w - 1 - index % g_sensor_w; // 左右翻转

        if (g_mapdel[index]) { curdis = 0; }
        if (curdis >= 964000 || curdis == 0) {
            px.push_back(NAN); py.push_back(NAN); pz.push_back(NAN); inten.push_back(0);
            continue;
        }
        float xx = g_mapx[index];
        float yy = g_mapy[index];
        double ax = (double)xx * xx;
        double ay = (double)yy * yy;
        double z = 0.001 * (double)curdis / sqrt(ax + ay + 1.0);
        double x = (double)xx * z;
        double y = (double)yy * z;
        y = -y; // !vmirror
        // 简化反射率: (amp*200/integ) * refcoef * dist²
        double ampv = amp[i];
        if (ampv >= 64000) { ampv = 0; }
        double refl = 0.0;
        if (integ != 0) {
            double a32 = (ampv * 200.0) / (double)integ;
            double dm = (double)curdis / 1000.0;
            refl = a32 * refcoef * dm * dm;
        } else {
            refl = ampv;
        }
        if (refl < 0) refl = 0;
        if (refl > 255) refl = 255;
        px.push_back((float)x); py.push_back((float)y); pz.push_back((float)z);
        inten.push_back((uint8_t)lround(refl));
    }
}

// ==================== Zenoh queryable 回调 ====================
static void file_query_handler(z_loaned_query_t *query, void *) {
    std::lock_guard<std::mutex> lock(g_files_mtx);
    if (!g_files_payload.empty()) {
        z_owned_bytes_t payload;
        z_bytes_copy_from_buf(&payload, g_files_payload.data(), g_files_payload.size());
        // zenoh-pico 1.10 ignores reply congestion/priority options and inherits them from the query.
        // Its reply transport is always Reliable + Block, so there is no effective per-reply override here.
        z_query_reply(query, z_query_keyexpr(query), z_move(payload), NULL);
    }
    g_files_requested = true;
}

static void refresh_files() {
    PBWriter array;
    // Fixed files only; never accept file paths from network requests.
    for (const char *name : {"config.json", "calibration.json", "lidar_intrinsics.json"}) {
        try {
            const auto data = std::string(name) == "calibration.json" ?
                std::string(PLACEHOLDER_CALIBRATION) + "\n" :
                xt::read_file(std::string(name) == "config.json" ? xt::config_path() :
                    xt::parent(xt::config_path()) + "/" + name);
            PBWriter file;
            enc_header(file, g_file_seq++, xt::protocol_monotonic_us(), 0);
            file.str(2, name);
            file.str(3, data);
            array.len(1, file.buf);
        } catch (const std::exception &e) {
            printf("[files] %s: %s\n", name, e.what());
        }
    }
    std::lock_guard<std::mutex> lock(g_files_mtx);
    g_files_payload = std::move(array.buf);
}

struct RadarPublication {
    bool pointcloud = false;
    bool image = false;
    uint64_t queued_ms = 0, frame = 0;
    uint32_t seq = 0;
    size_t total = 0, valid = 0;
    int temperature[2] = {};
    unsigned devstate = 0;
    int64_t source_stamp_us = 0;
    uint64_t raw_sensor_timestamp_ms = 0;
    std::vector<uint8_t> data, preview;
};
static int send_pointcloud_payload(const std::vector<uint8_t> &payload);
// Sensor queues are deliberately independent: a blocked image publisher can
// never consume pointcloud capacity, and vice versa.
static PublishQueue<RadarPublication, 32> g_image_publications;
static PublishQueue<RadarPublication, 32> g_pointcloud_publications;
static std::atomic<uint64_t> g_publish_expired{0}, g_publish_errors{0}, g_put_max_ms{0};

static void radar_publisher_main(PublishQueue<RadarPublication, 32> &queue) {
    RadarPublication job;
    while (true) {
        if (!queue.pop(job)) continue;
        const auto start = now_ms();
        if (start - job.queued_ms > 600) { ++g_publish_expired; continue; }
        if (g_ingest_only) {
            const int result = send_pointcloud_payload(job.data);
            if (result != Z_OK) ++g_publish_errors;
            continue;
        }
        z_owned_bytes_t payload;
        z_bytes_copy_from_buf(&payload, job.data.data(), job.data.size());
        int result = job.pointcloud ? z_publisher_put(z_loan(g_pub_pc), z_move(payload), NULL) :
            z_publisher_put(z_loan(g_pub_img), z_move(payload), NULL);
        if (result != Z_OK) ++g_publish_errors;
        int preview_result = Z_OK;
        if (job.pointcloud) {
            pc_stream_send(job.seq, job.data);
            if (g_preview_enabled) {
                z_owned_bytes_t preview;
                z_bytes_copy_from_buf(&preview, job.preview.data(), job.preview.size());
                preview_result = z_publisher_put(z_loan(g_pub_pc_preview), z_move(preview), NULL);
                if (preview_result != Z_OK) ++g_publish_errors;
            }
        }
        const auto elapsed = now_ms() - start;
        if (elapsed > g_put_max_ms.load()) g_put_max_ms = elapsed;
        if (job.pointcloud && (job.seq % 25 == 0 || result != Z_OK || elapsed > 200)) {
            printf("[zenoh] pointcloud seq=%u frame=%llu total=%zu valid=%zu bytes=%zu put_ret=%d preview_bytes=%zu preview_ret=%d temp_raw=%d,%d devstate=%u source_stamp_us=%lld raw_sensor_timestamp_ms=%llu queue_ms=%llu put_ms=%llu\n",
                job.seq, (unsigned long long)job.frame, job.total, job.valid, job.data.size(), result,
                job.preview.size(), preview_result, job.temperature[0], job.temperature[1], job.devstate,
                (long long)job.source_stamp_us, (unsigned long long)job.raw_sensor_timestamp_ms,
                (unsigned long long)(start-job.queued_ms), (unsigned long long)elapsed);
        }
    }
}

static bool read_all(int fd, uint8_t *data, size_t length) {
    while (length) {
        ssize_t received = recv(fd, data, length, 0);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) return false;
        data += received;
        length -= size_t(received);
    }
    return true;
}

static bool write_all(int fd, const uint8_t *data, size_t length) {
    while (length) {
        const ssize_t sent = send(fd, data, length, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return false;
        data += sent;
        length -= size_t(sent);
    }
    return true;
}

static int send_pointcloud_payload(const std::vector<uint8_t> &payload) {
    if (payload.empty() || payload.size() > 4 * 1024 * 1024) return -1;
    if (g_point_ingest_fd < 0) {
        g_point_ingest_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (g_point_ingest_fd < 0) return -1;
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        strncpy(address.sun_path, POINTCLOUD_INGEST_SOCKET, sizeof(address.sun_path) - 1);
        if (connect(g_point_ingest_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
            close(g_point_ingest_fd); g_point_ingest_fd = -1; return -1;
        }
        printf("[radar] pointcloud handoff connected %s\n", POINTCLOUD_INGEST_SOCKET);
    }
    const uint32_t length = htonl(uint32_t(payload.size()));
    if (write_all(g_point_ingest_fd, reinterpret_cast<const uint8_t *>(&length), sizeof(length)) &&
        write_all(g_point_ingest_fd, payload.data(), payload.size())) return Z_OK;
    close(g_point_ingest_fd); g_point_ingest_fd = -1;
    return -1;
}

static void image_ingest_main() {
    unlink(IMAGE_INGEST_SOCKET);
    int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener < 0) { printf("[image-ingest] socket failed: %s\n", strerror(errno)); return; }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, IMAGE_INGEST_SOCKET, sizeof(address.sun_path) - 1);
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(listener, 2) != 0) {
        printf("[image-ingest] bind/listen failed: %s\n", strerror(errno)); close(listener); return;
    }
    chmod(IMAGE_INGEST_SOCKET, 0660);
    printf("[image-ingest] listening %s\n", IMAGE_INGEST_SOCKET);
    while (true) {
        int client = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) { if (errno == EINTR) continue; continue; }
        while (true) {
            uint32_t length_be = 0;
            if (!read_all(client, reinterpret_cast<uint8_t *>(&length_be), sizeof(length_be))) break;
            const uint32_t length = ntohl(length_be);
            if (length == 0 || length > 1024 * 1024) { printf("[image-ingest] invalid payload=%u\n", length); break; }
            RadarPublication job;
            job.image = true;
            job.queued_ms = now_ms();
            job.data.resize(length);
            if (!read_all(client, job.data.data(), job.data.size())) break;
            g_image_publications.push(std::move(job));
        }
        close(client);
    }
}

static void pointcloud_ingest_main() {
    unlink(POINTCLOUD_INGEST_SOCKET);
    const int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener < 0) return;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, POINTCLOUD_INGEST_SOCKET, sizeof(address.sun_path) - 1);
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(listener, 2) != 0) {
        close(listener); return;
    }
    chmod(POINTCLOUD_INGEST_SOCKET, 0660);
    printf("[pointcloud-ingest] listening %s\n", POINTCLOUD_INGEST_SOCKET);
    while (true) {
        const int client = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) { if (errno == EINTR) continue; continue; }
        while (true) {
            uint32_t length_be = 0;
            if (!read_all(client, reinterpret_cast<uint8_t *>(&length_be), sizeof(length_be))) break;
            const uint32_t length = ntohl(length_be);
            if (length == 0 || length > 4 * 1024 * 1024) break;
            RadarPublication job;
            job.pointcloud = true;
            job.queued_ms = now_ms();
            job.data.resize(length);
            if (!read_all(client, job.data.data(), job.data.size())) break;
            g_pointcloud_publications.push(std::move(job));
        }
        close(client);
    }
}

static void file_publisher_main() {
    while (true) {
        if (g_files_requested.exchange(false)) {
            refresh_files();
            std::vector<uint8_t> bytes;
            { std::lock_guard<std::mutex> lock(g_files_mtx); bytes = g_files_payload; }
            if (!bytes.empty()) {
                z_owned_bytes_t payload;
                z_bytes_copy_from_buf(&payload, bytes.data(), bytes.size());
                int result = z_publisher_put(z_loan(g_pub_files), z_move(payload), NULL);
                printf("[files] bytes=%zu put=%d\n", bytes.size(), result);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

static int restart_camera_service_isolated() {
    const pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        // Never leak the radar Zenoh/control sockets into the restarted camera service.
        for (int fd = 3; fd < 4096; ++fd) close(fd);
        execl("/etc/init.d/S99xtcamera", "S99xtcamera", "restart", static_cast<char *>(nullptr));
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return status;
}

static int restart_radar_service_isolated() {
    const pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        for (int fd = 3; fd < 4096; ++fd) close(fd);
        execl("/etc/init.d/S99xtradar", "S99xtradar", "restart", static_cast<char *>(nullptr));
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return status;
}

static void query_handler(z_loaned_query_t *query, void *arg) {
    (void)arg;
    const z_loaned_keyexpr_t *qk = z_query_keyexpr(query);
    z_view_string_t vs;
    if (z_keyexpr_as_view_string(qk, &vs) != Z_OK) return;
    std::string key(z_string_data(z_loan(vs)), z_string_len(z_loan(vs)));
    std::string cmd = key.substr(key.rfind('/') + 1);
    std::vector<uint8_t> body;
    const z_loaned_bytes_t *payload = z_query_payload(query);
    if (payload) {
        z_owned_slice_t slice;
        if (z_bytes_to_slice(payload, &slice) != Z_OK) return;
        size_t size = z_slice_len(z_loan(slice));
        if (size > 4096) body = {0x80};
        else if (size) {
            auto p = z_slice_data(z_loan(slice));
            body.assign(p, p+size);
        }
        z_drop(z_move(slice));
    }
    PBWriter w;
    enc_header(w, g_cmd_seq++, xt::protocol_monotonic_us(), 0);
    unsigned code = 0;
    std::string desc = "success";
    bool reboot = false;
    bool restart_camera = false;
    bool restart_radar = false;
    std::array<int32_t,6> values{};
    bool valid = xt::parse_request(body.data(), body.size(), values, cmd == "setting");
    if (cmd == "setting") {
        std::lock_guard<std::mutex> lock(g_config_mtx);
        auto candidate = g_config;
        if (!valid) { code = 1; desc = "invalid SettingRequest protobuf"; }
        else if (values[0] != 0) { code = 1; desc = "IMU is unsupported on this hardware"; }
        else {
            for (size_t i=0; i<values.size(); ++i)
                if (values[i] != 0) candidate["sensors"][xt::setting_names[i]] = values[i];
            try { xt::validate(candidate); }
            catch (const std::exception &e) { code=1; desc=e.what(); }
            if (code == 0 && candidate != g_config) {
                restart_radar = candidate["sensors"]["lidar_fps"] != g_config["sensors"]["lidar_fps"];
                for (size_t i=2; i<values.size(); ++i)
                    restart_camera = restart_camera ||
                        candidate["sensors"][xt::setting_names[i]] != g_config["sensors"][xt::setting_names[i]];
                try {
                    xt::save_config(candidate);
                    g_config = candidate;
                    g_files_requested = true;
                } catch (const std::exception &e) { code=2; desc=e.what(); restart_camera=false; }
            }
        }
        for (size_t i=0; i<values.size(); ++i) w.s32(int(i)+2, g_config["sensors"][xt::setting_names[i]].get<int32_t>());
        xt::Json parameter = {{"apply",{{"camera",restart_camera ? "restart_scheduled" : "unchanged"},
                                          {"radar",restart_radar ? "restart_scheduled" : "unchanged"}}},
            {"active",g_active_config["sensors"]}, {"config_path",xt::config_path()},
            {"setting_image_format",{{"0","no change"},{"1","H264"},{"2","JPEG"}}},
            {"published_image_format",{{"H264",3},{"JPEG",2}}}};
        w.str(8, parameter.dump());
    } else if (cmd == "reboot") {
        if (!valid) { code=1; desc="invalid RebootRequest protobuf"; }
        else reboot=true;
    } else { code=1; desc="unknown cmd"; }
    enc_error(w, code, desc);
    z_owned_bytes_t reply;
    z_bytes_copy_from_buf(&reply, w.buf.data(), w.buf.size());
    // QoS is inherited from the query; zenoh-pico sends replies with Reliable + Block transport.
    int result = z_query_reply(query, qk, z_move(reply), NULL);
    if (restart_camera && code == 0 && result == Z_OK) {
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const int status = restart_camera_service_isolated();
            printf("[cfg] camera-only restart status=%d\n", status);
        }).detach();
    }
    if (restart_radar && code == 0 && result == Z_OK) {
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const int status = restart_radar_service_isolated();
            printf("[cfg] radar-only restart status=%d\n", status);
        }).detach();
    }
    if (reboot && result == Z_OK) {
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            system("reboot");
        }).detach();
    }
}

// ==================== 主流程 ====================
int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0); // 无缓冲输出便于日志
    g_gateway_mode = getenv("XT_GATEWAY_MODE") && strcmp(getenv("XT_GATEWAY_MODE"), "1") == 0;
    g_ingest_only = getenv("XT_RADAR_INGEST_ONLY") && strcmp(getenv("XT_RADAR_INGEST_ONLY"), "1") == 0;
    if (g_gateway_mode && g_ingest_only) {
        fprintf(stderr, "[cfg] gateway and radar-ingest modes are mutually exclusive\n"); return 1;
    }
    const bool has_device_sn = load_sn();
    try {
        g_config = g_active_config = xt::load_config(true);
        if (argc == 4 && strcmp(argv[1],"--network")==0) {
            g_config["radar_network"]={{"interface",argv[2]},{"source_address",argv[3]}};
            xt::save_config(g_config);
            printf("[cfg] radar network saved; restart xt_radar to apply\n");
            return 0;
        }
        if (argc!=1) throw std::runtime_error("usage: xt_radar [--network eth0|eth1 source_ipv4]");
        if (g_config.contains("radar_network")) {
            g_radar_interface = g_config["radar_network"]["interface"].get<std::string>();
            g_radar_source = g_config["radar_network"]["source_address"].get<std::string>();
            RADAR_IF = g_radar_interface.c_str();
            BOARD_IP = g_radar_source.c_str();
        }
        printf("[cfg] radar interface=%s source=%s\n",RADAR_IF,BOARD_IP);
        PC_HZ = g_config["sensors"]["lidar_fps"].get<int>();
    } catch (const std::exception &e) { fprintf(stderr,"[cfg] %s\n",e.what()); return 1; }

    if (g_gateway_mode && !has_device_sn) {
        fprintf(stderr, "[cfg] DEVICE_IDENTITY_NOT_INITIALIZED\n"); return 1;
    }
    if (!g_gateway_mode) {
        // Manufacturing initialization may use the real radar SN, but a
        // production gateway never waits for radar hardware to name topics.
        printf("[xt] 连接雷达 %s:%d ...\n", RADAR_IP, TCP_PORT);
        while (!tcp_connect_radar()) {
            printf("[xt] 雷达未连接, 5 秒后重试...\n");
            sleep(5);
        }
        std::vector<uint8_t> response;
        std::string radar_sn;
        const bool got_radar_sn = radar_cmd(4, {}, response) && extract_radar_sn(response, radar_sn);
        if (has_device_sn) {
            if (got_radar_sn && radar_sn != g_sn)
                printf("[cfg] radar_hw_sn=%s differs from existing device_sn=%s; existing value retained\n",
                       radar_sn.c_str(), g_sn.c_str());
            else
                printf("[cfg] radar_hw_sn=%s; existing device_sn retained\n",
                       got_radar_sn ? radar_sn.c_str() : "<unavailable>");
        } else {
            if (!got_radar_sn) {
                printf("[cfg] no valid device_sn and radar SN unavailable; refusing unnamed protocol topics\n");
                return 1;
            }
            if (!xt::initialize_device_sn(SN_FILE, radar_sn)) {
                printf("[cfg] device_sn initialization failed; original factory file was not replaced\n");
                return 1;
            }
            g_sn = radar_sn;
            printf("[cfg] initialized device_sn=%s from radar hardware SN\n", g_sn.c_str());
        }
    }

    if (g_ingest_only) {
        // Acquisition-only process: its publish path is the pointcloud Unix
        // socket, so gateway/session loss never terminates radar recovery.
        g_zenoh_ok = true;
        std::thread(radar_publisher_main, std::ref(g_pointcloud_publications)).detach();
        printf("[radar] acquisition-only mode; gateway handoff=%s\n", POINTCLOUD_INGEST_SOCKET);
    }

    // ---- Zenoh ----
    if (!g_ingest_only) {
    z_owned_config_t cfg;
    z_config_default(&cfg);
    configure_zenoh(cfg, g_active_config);
    int zr = z_open(&g_zs, z_move(cfg), NULL);
    printf("[zenoh] z_open ret=%d\n", zr);
    fflush(stdout);
    if (zr != Z_OK) {
        printf("[zenoh] 会话打开失败!\n");
        return 1;
    }
    {
        int rr = zp_start_read_task(z_loan_mut(g_zs), NULL);
        int lr = zp_start_lease_task(z_loan_mut(g_zs), NULL);
        printf("[zenoh] read_task ret=%d lease_task ret=%d\n", rr, lr);
    }
    g_zenoh_ok = true;
    printf("[zenoh] 会话已打开 (peer 模式)\n");

    char key[160];
    snprintf(key, sizeof key, "active/%s/image", g_sn.c_str());
    z_owned_keyexpr_t ke_img;
    z_keyexpr_from_str(&ke_img, key);
    auto image_qos = xt::fragmented_sensor_data_qos();
    if (z_declare_publisher(z_loan(g_zs), &g_pub_img, z_loan(ke_img), &image_qos) != Z_OK) {
        printf("[zenoh] image publisher failed\n"); return 1;
    }
    xt::log_fragmented_sensor_qos(key, image_qos);
    snprintf(key, sizeof key, "active/%s/pointcloud", g_sn.c_str());
    z_owned_keyexpr_t ke_pc;
    z_keyexpr_from_str(&ke_pc, key);
    auto pointcloud_qos = xt::fragmented_sensor_data_qos();
    if (z_declare_publisher(z_loan(g_zs), &g_pub_pc, z_loan(ke_pc), &pointcloud_qos) != Z_OK) {
        printf("[zenoh] 点云发布者失败\n"); return 1;
    }
    xt::log_fragmented_sensor_qos(key, pointcloud_qos);
    g_preview_enabled = getenv("XT_PCLOUD_PREVIEW") && strcmp(getenv("XT_PCLOUD_PREVIEW"), "1") == 0;
    if (g_preview_enabled) {
        snprintf(key, sizeof key, "active/%s/pointcloud_preview", g_sn.c_str());
        z_owned_keyexpr_t ke_pc_preview;
        z_keyexpr_from_str(&ke_pc_preview, key);
        auto preview_qos = xt::sensor_data_qos();
        if (z_declare_publisher(z_loan(g_zs), &g_pub_pc_preview, z_loan(ke_pc_preview), &preview_qos) != Z_OK) {
            printf("[zenoh] 点云预览发布者失败\n"); return 1;
        }
        xt::log_publisher_qos(key, preview_qos);
    }
    snprintf(key, sizeof key, "active/%s/cmd/*", g_sn.c_str());
    z_owned_keyexpr_t ke_cmd;
    z_keyexpr_from_str(&ke_cmd, key);
    z_owned_closure_query_t cb;
    z_closure_query(&cb, query_handler, NULL, NULL);
    z_declare_queryable(z_loan(g_zs), &g_qable, z_loan(ke_cmd), z_move(cb), NULL);
    printf("[zenoh] publishers active/%s/image, active/%s/pointcloud ready; preview=%s\n",
           g_sn.c_str(), g_sn.c_str(), g_preview_enabled ? "enabled" : "disabled");
    pc_stream_init();
    snprintf(key, sizeof key, "active/%s/config_file", g_sn.c_str());
    z_owned_keyexpr_t ke_files;
    z_keyexpr_from_str(&ke_files, key);
    auto config_file_qos = xt::reliable_data_qos();
    if (z_declare_publisher(z_loan(g_zs), &g_pub_files, z_loan(ke_files), &config_file_qos) != Z_OK) return 1;
    xt::log_publisher_qos(key, config_file_qos);
    refresh_files();
    z_owned_closure_query_t files_cb;
    z_closure_query(&files_cb, file_query_handler, NULL, NULL);
    if (z_declare_queryable(z_loan(g_zs), &g_file_queryable, z_loan(ke_files), z_move(files_cb), NULL) != Z_OK) return 1;
    std::thread(file_publisher_main).detach();
    std::thread(radar_publisher_main, std::ref(g_image_publications)).detach();
    std::thread(radar_publisher_main, std::ref(g_pointcloud_publications)).detach();
    std::thread(image_ingest_main).detach();
    if (g_gateway_mode) {
        std::thread(pointcloud_ingest_main).detach();
        printf("[gateway] sensor-neutral session ready\n");
        while (true) sleep(60);
    }
    }

    // cmd 0: 测试通信
    {
        std::vector<uint8_t> resp;
        if (radar_cmd(0, {}, resp)) printf("[xt] 通信测试 OK\n");
        else printf("[xt] 通信测试失败\n");
    }
    // cmd 18: 获取相机内参
    {
        std::vector<uint8_t> resp;
        if (radar_cmd(18, {}, resp) && resp.size() >= 36) {
            memcpy(&g_fx, resp.data() + 0, 4);
            memcpy(&g_fy, resp.data() + 4, 4);
            memcpy(&g_cx, resp.data() + 8, 4);
            memcpy(&g_cy, resp.data() + 12, 4);
            memcpy(&g_k1, resp.data() + 16, 4);
            memcpy(&g_k2, resp.data() + 20, 4);
            memcpy(&g_k3, resp.data() + 24, 4);
            memcpy(&g_p1, resp.data() + 28, 4);
            memcpy(&g_p2, resp.data() + 32, 4);
            printf("[xt] 内参 fx=%.3f fy=%.3f cx=%.3f cy=%.3f\n", g_fx, g_fy, g_cx, g_cy);
            try {
                const float values[] = {g_fx,g_fy,g_cx,g_cy,g_k1,g_k2,g_k3,g_p1,g_p2};
                bool finite = std::all_of(std::begin(values),std::end(values), [](float x){return std::isfinite(x);});
                if (!finite || g_fx <= 0 || g_fy <= 0) throw std::runtime_error("invalid measured intrinsics");
                xt::Json data = {{"source","XT-M60 TCP command 18"}, {"kind","lidar_intrinsics_only"},
                    {"fx",g_fx},{"fy",g_fy},{"cx",g_cx},{"cy",g_cy},
                    {"k1",g_k1},{"k2",g_k2},{"k3",g_k3},{"p1",g_p1},{"p2",g_p2},
                    {"camera_lidar_extrinsics_available",false}};
                xt::atomic_write(xt::parent(xt::config_path())+"/lidar_intrinsics.json",data.dump(2)+"\n");
                g_files_requested = true;
            } catch (const std::exception &e) { printf("[files] measured intrinsics not exported: %s\n",e.what()); }
        } else {
            printf("[xt] 内参获取失败, 使用 M60 默认值\n");
        }
    }
    // cmd4 was already used before Zenoh topic declaration to establish device_sn.
    build_maptable();

    if (configure_radar_udp()) printf("[xt] UDP 目标已设置 %s:%d\n", BOARD_IP, UDP_PORT);
    // cmd 1: 开始测量 (Depth+Amplitude, 连续)
    {
        g_debug = true;
        std::vector<uint8_t> resp;
        if (radar_cmd(1, {0x02, 0x01}, resp)) {
            printf("[xt] 开始测量 OK, resp=%zu bytes\n", resp.size());
        } else {
            printf("[xt] 开始测量失败 resp=%zu bytes:", resp.size());
            for (auto b : resp) printf(" %02x", b);
            printf("\n");
        }
        g_debug = false;
    }

    // ---- UDP 接收 (加入雷达多播组 239.255.255.76) ----
    int udpfd = socket(AF_INET, SOCK_DGRAM, 0);
    int reuse = 1;
    setsockopt(udpfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    int receive_buffer = 2*1024*1024;
    setsockopt(udpfd, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer));
    struct sockaddr_in ua;
    memset(&ua, 0, sizeof(ua));
    ua.sin_family = AF_INET;
    ua.sin_port = htons(UDP_PORT);
    ua.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(udpfd, (struct sockaddr *)&ua, sizeof(ua)) != 0) {
        printf("[xt] UDP bind %d 失败!\n", UDP_PORT);
        return 1;
    }
    // 加入雷达数据多播组 (SDK: 239.255.255.76, 走 eth1)
    {
        struct ip_mreqn mreq;
        memset(&mreq, 0, sizeof(mreq));
        mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.76");
        mreq.imr_address.s_addr = htonl(INADDR_ANY);
        mreq.imr_ifindex = if_nametoindex(RADAR_IF);
        if (setsockopt(udpfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
            printf("[xt] 加入多播组失败 errno=%d, 尝试默认接口\n", errno);
            struct ip_mreq m2;
            m2.imr_multiaddr.s_addr = inet_addr("239.255.255.76");
            m2.imr_interface.s_addr = htonl(INADDR_ANY);
            setsockopt(udpfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m2, sizeof(m2));
        } else {
            printf("[xt] 已加入多播组 239.255.255.76 (%s)\n", RADAR_IF);
        }
    }
    printf("[xt] UDP %d 监听中 (多播), 等待点云数据...\n", UDP_PORT);

    uint8_t buf[1500];
    uint64_t frames = 0;
    uint64_t udp_pkts = 0;
    uint64_t last_frame_ms = now_ms();
    uint64_t next_recovery_ms = 0;
    uint64_t next_network_check_ms = now_ms()+5000;
    uint64_t next_health_log_ms = now_ms()+5000;
    int retry_count = 0;
    int measurement_retries = 0;
    while (true) {
        drain_control_events();
        struct timeval tv = {0, 200000};
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(udpfd, &rfds);
        int sel = select(udpfd + 1, &rfds, NULL, NULL, &tv);
        if (sel > 0) {
            ssize_t n = recv(udpfd, buf, sizeof(buf), 0);
            if (n > 0) {
                udp_pkts++;
                if (udp_pkts <= 5) {
                    printf("[xt] UDP包#%llu %zu字节: %02x %02x %02x %02x ...\n",
                           (unsigned long long)udp_pkts, (size_t)n, buf[0], buf[1], buf[2], buf[3]);
                }
                std::vector<uint8_t> frame, ignored_imu;
                bool isf = false, isi = false;
                // IMU packets may be present in the transport, but this sample
                // has no validated IMU source and must not publish them.
                assemble_udp(buf, (size_t)n, frame, isf, isi, ignored_imu);
                if (isf) {
                    uint16_t width, height;
                    std::vector<uint32_t> dist;
                    std::vector<uint16_t> amp;
                    uint64_t ts_s;
                    uint32_t ts_ns;
                    FrameInfo finfo;
                    if (parse_frame(frame, width, height, dist, amp, ts_s, ts_ns, finfo)) {
                        ++frames;
                        if (retry_count) printf("[recovery] valid frames resumed after %d attempts\n", retry_count);
                        last_frame_ms = now_ms();
                        retry_count = 0;
                        measurement_retries = 0;
                        next_recovery_ms = 0;
                        std::vector<float> px, py, pz;
                        std::vector<uint8_t> inten;
                        compute_points(dist, amp, width, height, finfo, px, py, pz, inten);
                        size_t valid = 0;
                        for (auto v : pz) if (!std::isnan(v)) valid++;
                        if (g_zenoh_ok) {
                            uint64_t now = now_ms();
                            uint64_t last = g_last_pc_ms.load();
                            if (now >= last) {
                                {
                                    uint64_t period = uint64_t(1000.0/PC_HZ);
                                    g_last_pc_ms.store(last && now-last < period ? last+period : now+period);
                                    uint32_t seq = g_pc_seq++;
                                    RadarPublication job;
                                    job.pointcloud = true;
                                    job.seq = seq;
                                    job.frame = frames;
                                    job.total = px.size();
                                    job.valid = valid;
                                    job.temperature[0] = int(finfo.temperature[0]);
                                    job.temperature[1] = int(finfo.temperature[1]);
                                    job.devstate = unsigned(finfo.devstate);
                                    job.queued_ms = now_ms();
                                    const int64_t source_stamp_us = xt::protocol_monotonic_us();
                                    const uint64_t raw_sensor_timestamp_ms = ts_s * 1000ULL + ts_ns / 1000000U;
                                    job.source_stamp_us = source_stamp_us;
                                    job.raw_sensor_timestamp_ms = raw_sensor_timestamp_ms;
                                    job.data = build_pointcloud_msg(px, py, pz, inten, source_stamp_us, seq);
                                    job.preview = build_pointcloud_preview_msg(px, py, pz, inten,
                                                                                source_stamp_us,
                                                                                seq, valid);
                                    g_pointcloud_publications.push(std::move(job));
                                }
                            }
                        }
                    } else {
                        printf("[xt] 帧解析失败 size=%zu\n", frame.size());
                    }
                }
            }
        }
        // 若长时间无数据, 重发 UDP 目标设置并重启测量 (无限重试, 支持雷达热插拔)
        if ((now_ms() - last_frame_ms) > 3000 && now_ms() >= next_recovery_ms) {
            ++retry_count;
            printf("[recovery] no valid frames for %llums attempt=%d total_frames=%llu\n",
                (unsigned long long)(now_ms()-last_frame_ms), retry_count, (unsigned long long)frames);
            for (auto &f : g_frames) f.used=false;
            ip_mreqn mreq{};
            mreq.imr_multiaddr.s_addr=inet_addr("239.255.255.76");
            mreq.imr_ifindex=if_nametoindex(RADAR_IF);
            setsockopt(udpfd,IPPROTO_IP,IP_DROP_MEMBERSHIP,&mreq,sizeof mreq);
            setsockopt(udpfd,IPPROTO_IP,IP_ADD_MEMBERSHIP,&mreq,sizeof mreq);
            std::vector<uint8_t> resp;
            bool started=false;
            if (tcp_connect_radar()) {
                ++measurement_retries;
                // A reconnect/restart is enough for most outages. Reset only after repeated failures.
                bool stopped=radar_cmd(2,{},resp);
                if (!stopped) tcp_connect_radar();
                // A disconnected cable is not evidence that the sensor needs a reset.
                if (measurement_retries % 3 == 0) {
                    radar_cmd(13,{},resp);
                    std::this_thread::sleep_for(std::chrono::seconds(4));
                    if (tcp_connect_radar()) radar_cmd(0,{},resp);
                }
                started=configure_radar_udp() && radar_cmd(1,{0x02,0x01},resp);
            }
            printf("[recovery] start_sent=%d; waiting for actual valid frame\n",started);
            next_recovery_ms=now_ms()+uint64_t(std::min(retry_count,4))*2000;
        }
        // UDP can resume before the retry timer, while the control route is still missing.
        // Check without sending control commands or rewriting a healthy route during measurement.
        if (now_ms() >= next_network_check_ms) {
            ensure_radar_network();
            next_network_check_ms=now_ms()+5000;
        }
        if (now_ms() >= next_health_log_ms) {
            const auto image_queue = g_image_publications.stats();
            const auto pointcloud_queue = g_pointcloud_publications.stats();
            printf("[capture] frames=%llu udp=%llu age_ms=%llu image_queued=%zu image_drop=%llu pointcloud_queued=%zu pointcloud_drop=%llu expired=%llu put_errors=%llu put_max_ms=%llu\n",
                (unsigned long long)frames, (unsigned long long)udp_pkts,
                (unsigned long long)(now_ms()-last_frame_ms), image_queue.first,
                (unsigned long long)image_queue.second, pointcloud_queue.first,
                (unsigned long long)pointcloud_queue.second, (unsigned long long)g_publish_expired.load(),
                (unsigned long long)g_publish_errors.load(), (unsigned long long)g_put_max_ms.load());
            next_health_log_ms=now_ms()+5000;
        }
    }
    return 0;
}
