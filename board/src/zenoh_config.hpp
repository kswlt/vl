#pragma once
#include "device_config.hpp"
#ifdef XT_USE_ZENOHC
#include <zenoh.h>
#else
#include "zenoh-pico.h"
#endif

inline void configure_zenoh(z_owned_config_t &cfg, const xt::Json &config) {
    const auto &z = config.at("zenoh");
#ifdef XT_USE_ZENOHC
    auto put = [&](const char *key, const xt::Json &value) {
        const std::string encoded = value.dump();
        if (zc_config_insert_json5(z_loan_mut(cfg), key, encoded.c_str()) != Z_OK)
            throw std::runtime_error("zenoh config insert failed");
    };
    put("mode", z.at("mode"));
    put("listen/endpoints", z.at("listen"));
    put("connect/endpoints", z.at("connect"));
    put("scouting/multicast/enabled", z.at("scouting").at("multicast").at("enabled"));
#else
    auto put = [&](uint8_t key, const std::string &value) {
        if (zp_config_insert(z_loan_mut(cfg), key, value.c_str()) != Z_OK)
            throw std::runtime_error("zenoh config insert failed");
    };
    put(Z_CONFIG_MODE_KEY, z.at("mode").get<std::string>());
    const auto &listen = z.at("listen");
    if (!listen.empty()) put(Z_CONFIG_LISTEN_KEY, listen[0].get<std::string>());
    if (!z.at("connect").empty()) put(Z_CONFIG_CONNECT_KEY, z.at("connect")[0].get<std::string>());
    put(Z_CONFIG_MULTICAST_SCOUTING_KEY, z.at("scouting").at("multicast").at("enabled").get<bool>() ? "true" : "false");
#endif
}
