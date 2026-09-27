#pragma once

#include <atomic>
#include <cstdint>
#include <ctime>
#include <stdexcept>
#include <string>

namespace xt {

inline std::atomic<bool> g_protocol_unix_epoch{false};

// Select the timestamp contract before any publishers start.  unix_epoch must
// follow the board's current realtime clock.  Do not cache a realtime-minus-
// monotonic offset: camera and radar can start producing frames hours apart,
// and chrony may have corrected CLOCK_REALTIME between those two events.
inline void configure_protocol_clock(const std::string &mode) {
    if (mode == "monotonic") {
        g_protocol_unix_epoch.store(false);
        return;
    }
    if (mode != "unix_epoch") throw std::runtime_error("timestamp_mode must be monotonic or unix_epoch");
    timespec realtime{};
    if (clock_gettime(CLOCK_REALTIME, &realtime) != 0 || realtime.tv_sec < 1577836800LL)
        throw std::runtime_error("unix_epoch requested but board realtime is earlier than 2020-01-01");
    g_protocol_unix_epoch.store(true);
}

inline int64_t protocol_monotonic_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000000LL + int64_t(ts.tv_nsec) / 1000LL;
}

inline int64_t protocol_timestamp_us() {
    if (!g_protocol_unix_epoch.load()) return protocol_monotonic_us();
    timespec realtime{};
    clock_gettime(CLOCK_REALTIME, &realtime);
    return int64_t(realtime.tv_sec) * 1000000LL + int64_t(realtime.tv_nsec) / 1000LL;
}

}
