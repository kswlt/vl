#pragma once

#include <cstdio>

#ifdef XT_USE_ZENOHC
#include <zenoh.h>
#else
#include "zenoh-pico.h"
#endif

#ifndef Z_FEATURE_UNSTABLE_API
#error "Zenoh BestEffort reliability requires Z_FEATURE_UNSTABLE_API"
#endif

namespace xt {

static_assert(Z_PRIORITY_DATA == 5, "Protocol Data priority must map to Zenoh Data");
static_assert(Z_PRIORITY_REAL_TIME == 1, "Unexpected zenoh-pico RealTime priority value");

inline z_publisher_options_t publisher_qos(z_congestion_control_t congestion,
                                           z_priority_t priority,
                                           z_reliability_t reliability) {
    z_publisher_options_t options;
    z_publisher_options_default(&options);
    options.congestion_control = congestion;
    options.priority = priority;
    options.reliability = reliability;
    return options;
}

inline z_publisher_options_t sensor_data_qos() {
    return publisher_qos(Z_CONGESTION_CONTROL_DROP, Z_PRIORITY_DATA,
                         Z_RELIABILITY_BEST_EFFORT);
}

// zenoh-pico 1.10.0 passes a boolean to its fragmented-frame constructor,
// inverting Reliable=0 and BestEffort=1 on the wire. Image and full pointcloud
// payloads always fragment with the configured 2048-byte unicast batch.
inline z_publisher_options_t fragmented_sensor_data_qos() {
    return publisher_qos(Z_CONGESTION_CONTROL_DROP, Z_PRIORITY_DATA,
                         Z_RELIABILITY_RELIABLE);
}

// Compatibility streams prioritize complete delivery over stale frames. The
// zenoh-pico 1.10.0 fragmented-frame constructor inverts this enum on the wire;
// official zenoh-c does not. Keep the workaround confined to zenoh-pico.
inline z_publisher_options_t fragmented_reliable_data_qos() {
#ifdef XT_USE_ZENOHC
    return publisher_qos(Z_CONGESTION_CONTROL_BLOCK, Z_PRIORITY_DATA,
                         Z_RELIABILITY_RELIABLE);
#else
    return publisher_qos(Z_CONGESTION_CONTROL_BLOCK, Z_PRIORITY_DATA,
                         Z_RELIABILITY_BEST_EFFORT);
#endif
}

inline z_publisher_options_t realtime_sensor_qos() {
    return publisher_qos(Z_CONGESTION_CONTROL_DROP, Z_PRIORITY_REAL_TIME,
                         Z_RELIABILITY_BEST_EFFORT);
}

inline z_publisher_options_t reliable_data_qos() {
    return publisher_qos(Z_CONGESTION_CONTROL_BLOCK, Z_PRIORITY_DATA,
                         Z_RELIABILITY_RELIABLE);
}

inline const char *congestion_name(z_congestion_control_t value) {
    return value == Z_CONGESTION_CONTROL_BLOCK ? "block" : "drop";
}

inline const char *priority_name(z_priority_t value) {
    if (value == Z_PRIORITY_REAL_TIME) return "real_time";
    if (value == Z_PRIORITY_DATA) return "data";
    return "other";
}

inline const char *reliability_name(z_reliability_t value) {
    return value == Z_RELIABILITY_RELIABLE ? "reliable" : "best_effort";
}

inline void log_publisher_qos(const char *topic, const z_publisher_options_t &options) {
    std::printf("[qos] %s congestion=%s(%d) priority=%s(%d) reliability=%s(%d)\n",
                topic, congestion_name(options.congestion_control), int(options.congestion_control),
                priority_name(options.priority), int(options.priority),
                reliability_name(options.reliability), int(options.reliability));
}

inline void log_fragmented_sensor_qos(const char *topic,
                                      const z_publisher_options_t &options) {
    std::printf("[qos] %s congestion=%s(%d) priority=%s(%d) "
                "api_reliability=%s(%d) wire_reliability=best_effort(1) "
                "workaround=zenoh-pico-1.10.0-fragment-enum\n",
                topic, congestion_name(options.congestion_control), int(options.congestion_control),
                priority_name(options.priority), int(options.priority),
                reliability_name(options.reliability), int(options.reliability));
}

inline void log_fragmented_reliable_qos(const char *topic,
                                        const z_publisher_options_t &options) {
#ifdef XT_USE_ZENOHC
    log_publisher_qos(topic, options);
#else
    std::printf("[qos] %s congestion=%s(%d) priority=%s(%d) "
                "api_reliability=%s(%d) wire_reliability=reliable(0) "
                "workaround=zenoh-pico-1.10.0-fragment-enum\n",
                topic, congestion_name(options.congestion_control), int(options.congestion_control),
                priority_name(options.priority), int(options.priority),
                reliability_name(options.reliability), int(options.reliability));
#endif
}

}  // namespace xt
