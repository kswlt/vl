#pragma once
#include <array>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <nlohmann/json.hpp>

namespace xt {
using Json = nlohmann::json;
inline bool valid_topic_sn(const std::string &sn) {
    if (sn.empty() || sn == "UNCONFIGURED" || sn.size() > 64) return false;
    for (unsigned char c : sn)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
inline std::string config_path() {
    const char *p = getenv("XT_CONFIG_FILE");
    return p && *p ? p : "/userdata/xtapp/config.json";
}
inline std::string parent(const std::string &p) {
    auto pos = p.rfind('/');
    return pos == std::string::npos ? "." : pos == 0 ? "/" : p.substr(0, pos);
}
inline std::string read_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    std::string bytes;
    char block[4096];
    while (f) {
        f.read(block, sizeof block);
        bytes.append(block, static_cast<size_t>(f.gcount()));
        if (bytes.size() > 65536) throw std::runtime_error("config file exceeds 64 KiB");
    }
    if (!f.eof()) throw std::runtime_error("read failed " + path);
    return bytes;
}
inline void atomic_write(const std::string &path, const std::string &bytes) {
    const std::string dir = parent(path);
    int dfd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) throw std::runtime_error("cannot open config directory");
    std::string name = path + ".tmp.XXXXXX";
    std::vector<char> tmp(name.begin(), name.end());
    tmp.push_back(0);
    int fd = mkstemp(tmp.data());
    bool renamed = false;
    try {
        if (fd < 0) throw std::runtime_error("cannot create temporary config");
        if (fchmod(fd, 0644) != 0) throw std::runtime_error("config chmod failed");
        size_t off = 0;
        while (off < bytes.size()) {
            ssize_t n = write(fd, bytes.data() + off, bytes.size() - off);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("config write failed");
            off += static_cast<size_t>(n);
        }
        if (fsync(fd) != 0) throw std::runtime_error("config fsync failed");
        close(fd); fd = -1;
        if (rename(tmp.data(), path.c_str()) != 0) throw std::runtime_error("config rename failed");
        renamed = true;
        if (fsync(dfd) != 0) throw std::runtime_error("directory fsync failed; durability uncertain");
        close(dfd);
    } catch (...) {
        if (fd >= 0) close(fd);
        if (!renamed) unlink(tmp.data());
        close(dfd);
        throw;
    }
}
inline Json defaults() {
    return {{"version", 1}, {"sensors", {{"imu_fps",0}, {"lidar_fps",5},
        {"image_fps",10}, {"image_format",2}, {"h264_gop",20}, {"h264_bitrate",4},
        {"image_jpeg_source","snapshot"}}},
        {"timestamp_mode", "monotonic"},
        {"legacy_compatibility", {{"enabled",false}, {"legacy_sn","LEGACY00000001"},
            {"http_port",8080}, {"image_batch_size",3}, {"legacy_topics_only",false},
            {"strict_setting_response",false},
            {"pointcloud_axes","native"}}},
        {"zenoh", {{"mode","peer"}, {"connect", Json::array()},
            {"listen", Json::array({"tcp/0.0.0.0:7447"})},
            {"scouting", {{"multicast", {{"enabled",true}}}}}}},
        {"camera_id", "UNCONFIGURED"}, {"fusion_camera_id", "UNCONFIGURED"}};
}
inline const std::array<const char *,6> setting_names = {
    "imu_fps","lidar_fps","image_fps","image_format","h264_gop","h264_bitrate"};
inline void validate(const Json &j) {
    if (!j.is_object() || j.at("version") != 1) throw std::runtime_error("invalid config version");
    if (j.contains("radar_network")) {
        const auto &n=j.at("radar_network");
        if (n.at("interface")!="eth0" && n.at("interface")!="eth1")
            throw std::runtime_error("radar interface must be eth0 or eth1");
        if (!n.at("source_address").is_string()) throw std::runtime_error("invalid radar source address");
        in_addr address{};
        const auto text=n.at("source_address").get<std::string>();
        if (inet_pton(AF_INET,text.c_str(),&address)!=1 || ntohl(address.s_addr)==0 ||
            (ntohl(address.s_addr)>>24)>=224 || (ntohl(address.s_addr)>>24)==127)
            throw std::runtime_error("radar source must be a unicast IPv4 address");
    }
    const auto &s = j.at("sensors");
    // XT-M60 product manual specifies a 1..30 frame/s range. Keep the
    // protocol limiter aligned with the sensor instead of capping at 15.
    const int low[] = {0,1,1,1,0,0}, high[] = {0,30,60,3,200,12};
    for (size_t i=0; i<setting_names.size(); ++i) {
        const auto &v = s.at(setting_names[i]);
        if (!v.is_number_integer() || v < low[i] || v > high[i])
            throw std::runtime_error(std::string("unsupported ") + setting_names[i]);
    }
    // image_format now uses the ImageMsg.format space (2=JPEG, 3=H264);
    // legacy value 1 (H264) is still accepted for rollback compatibility.
    if ((s.at("image_format") == 1 || s.at("image_format") == 3) && s.at("image_fps") < 5)
        throw std::runtime_error("MC800S H264 image_fps must be 5..60");
    const std::string jpeg_source = s.value("image_jpeg_source", "snapshot");
    if (jpeg_source != "snapshot" && jpeg_source != "transcode")
        throw std::runtime_error("image_jpeg_source must be snapshot or transcode");
    if (s.at("image_format") == 2 && jpeg_source == "snapshot" && s.at("image_fps") > 10)
        throw std::runtime_error("MC800S JPEG snapshot image_fps must be 1..10");
    if (s.at("image_format") == 2 && jpeg_source == "transcode" && s.at("image_fps") < 5)
        throw std::runtime_error("MC800S JPEG transcode image_fps must be 5..60");
    if (s.at("imu_fps") != 0) throw std::runtime_error("IMU is unsupported on this hardware");
    const std::string timestamp_mode = j.value("timestamp_mode", "monotonic");
    if (timestamp_mode != "monotonic" && timestamp_mode != "unix_epoch")
        throw std::runtime_error("timestamp_mode must be monotonic or unix_epoch");
    if (j.contains("legacy_compatibility")) {
        const auto &compat = j.at("legacy_compatibility");
        if (!compat.is_object() ||
            (compat.contains("enabled") && !compat.at("enabled").is_boolean()))
            throw std::runtime_error("invalid legacy_compatibility");
        const std::string legacy_sn = compat.value("legacy_sn", "");
        if (compat.value("enabled", false) && !valid_topic_sn(legacy_sn))
            throw std::runtime_error("legacy_sn must be a valid single topic segment");
        const int http_port = compat.value("http_port", 8080);
        if (http_port < 1 || http_port > 65535) throw std::runtime_error("invalid compatibility http_port");
        const int image_batch_size = compat.value("image_batch_size", 3);
        if (image_batch_size < 1 || image_batch_size > 10)
            throw std::runtime_error("invalid compatibility image_batch_size");
        if (compat.contains("legacy_topics_only") && !compat.at("legacy_topics_only").is_boolean())
            throw std::runtime_error("invalid compatibility legacy_topics_only");
        if (compat.contains("strict_setting_response") &&
            !compat.at("strict_setting_response").is_boolean())
            throw std::runtime_error("invalid compatibility strict_setting_response");
        const std::string axes = compat.value("pointcloud_axes", "native");
        if (axes != "native" && axes != "legacy_x_forward_y_left_z_up")
            throw std::runtime_error("invalid pointcloud_axes");
    }
    for (const auto *channel : {&j.at("zenoh")}) {
        const auto &z = *channel;
        if (z.at("mode") != "peer" && z.at("mode") != "client")
            throw std::runtime_error("zenoh-pico supports peer/client mode");
        if (!z.at("connect").is_array() || z.at("connect").size() > 1 ||
            !z.at("scouting").at("multicast").at("enabled").is_boolean())
            throw std::runtime_error("invalid zenoh connect/scouting");
        for (const auto &e : z.at("connect"))
            if (!e.is_string() || e.get<std::string>().rfind("tcp/",0) != 0)
                throw std::runtime_error("invalid TCP endpoint");
        const auto &endpoints = z.at("listen");
        if (!endpoints.is_array() || endpoints.size() > 1) throw std::runtime_error("invalid listen array");
        for (const auto &e : endpoints)
            if (!e.is_string() || e.get<std::string>().rfind("tcp/",0) != 0)
                throw std::runtime_error("invalid listen endpoint");
    }
    for (const char *id : {"camera_id", "fusion_camera_id"})
        if (!j.contains(id) || !j.at(id).is_string() || j.at(id).get<std::string>().empty())
            throw std::runtime_error(std::string("invalid ") + id);
}
inline Json load_config(bool create = false) {
    const auto path = config_path();
    // Both services may start together. Serialize default creation and future writers.
    int fd = open((path + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) throw std::runtime_error("cannot open config lock");
    if (flock(fd, LOCK_EX) != 0) { close(fd); throw std::runtime_error("config lock failed"); }
    try {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            if (errno != ENOENT || !create) throw std::runtime_error("config missing");
            atomic_write(path, defaults().dump(2) + "\n");
        }
        auto j = Json::parse(read_file(path));
        // Older deployments stored imu_fps=50 although this hardware has no
        // usable IMU. Migrate only that field atomically before validation.
        if (j.contains("sensors") && j["sensors"].is_object() &&
            j["sensors"].contains("imu_fps") && j["sensors"]["imu_fps"] != 0) {
            j["sensors"]["imu_fps"] = 0;
            atomic_write(path, j.dump(2) + "\n");
        }
        validate(j);
        close(fd);
        return j;
    } catch (...) { close(fd); throw; }
}
inline void save_config(const Json &j) {
    validate(j);
    const auto path = config_path();
    int fd = open((path + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) throw std::runtime_error("cannot open config lock");
    if (flock(fd, LOCK_EX) != 0) { close(fd); throw std::runtime_error("config lock failed"); }
    try { atomic_write(path, j.dump(2) + "\n"); close(fd); }
    catch (...) { close(fd); throw; }
}
}
