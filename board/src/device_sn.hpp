#pragma once

#include <cctype>
#include <fstream>
#include <string>

#include "device_config.hpp"

namespace xt {

inline std::string trim_yaml_scalar(const std::string &value) {
    const size_t first = value.find_first_not_of(" \t\r\n\"");
    const size_t last = value.find_last_not_of(" \t\r\n\"");
    if (first == std::string::npos) return {};
    return value.substr(first, last - first + 1);
}

inline bool valid_device_sn(const std::string &sn) {
    return valid_topic_sn(sn);
}

inline bool yaml_device_sn(const std::string &yaml, std::string &sn) {
    size_t at = 0;
    while (at <= yaml.size()) {
        const size_t end = yaml.find('\n', at);
        const size_t length = end == std::string::npos ? yaml.size() - at : end - at;
        const std::string line = yaml.substr(at, length);
        const size_t key = line.find_first_not_of(" \t\r");
        if (key != std::string::npos && line[key] != '#') {
            const size_t colon = line.find(':', key);
            if (colon != std::string::npos && line.substr(key, colon - key) == "device_sn") {
                const std::string value = trim_yaml_scalar(line.substr(colon + 1));
                if (valid_device_sn(value)) { sn = value; return true; }
            }
        }
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return false;
}

inline bool load_existing_device_sn(const std::string &path, std::string &sn) {
    try { return yaml_device_sn(read_file(path), sn); }
    catch (...) { return false; }
}

// Replace only device_sn and atomically preserve every other YAML field/byte.
inline bool initialize_device_sn(const std::string &path, const std::string &sn) {
    if (!valid_device_sn(sn)) return false;
    std::string original;
    try { original = read_file(path); }
    catch (...) {
        std::ifstream probe(path);
        if (probe.good()) return false;
    }
    std::string updated;
    bool replaced = false;
    size_t at = 0;
    while (at <= original.size()) {
        const size_t end = original.find('\n', at);
        const size_t length = end == std::string::npos ? original.size() - at : end - at;
        const std::string line = original.substr(at, length);
        const size_t key = line.find_first_not_of(" \t\r");
        if (!replaced && key != std::string::npos && line[key] != '#') {
            const size_t colon = line.find(':', key);
            if (colon != std::string::npos && line.substr(key, colon - key) == "device_sn") {
                updated.append(line, 0, key);
                updated += "device_sn: \"" + sn + "\"";
                if (end != std::string::npos && length && line.back() == '\r') updated += '\r';
                replaced = true;
            } else updated += line;
        } else updated += line;
        if (end == std::string::npos) break;
        updated += '\n';
        at = end + 1;
    }
    if (!replaced) {
        if (!updated.empty() && updated.back() != '\n') updated += '\n';
        updated += "device_sn: \"" + sn + "\"\n";
    }
    try {
        atomic_write(path, updated);
        std::string verified;
        return load_existing_device_sn(path, verified) && verified == sn;
    } catch (...) { return false; }
}

} // namespace xt
