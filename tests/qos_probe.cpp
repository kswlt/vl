#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "zenoh-pico.h"

#ifndef Z_FEATURE_UNSTABLE_API
#error "qos_probe requires Z_FEATURE_UNSTABLE_API"
#endif

struct ProbeState {
    std::atomic<int> received{0};
    std::atomic<int> mismatches{0};
    int expected_count = 0;
    z_congestion_control_t congestion = Z_CONGESTION_CONTROL_DROP;
    z_priority_t priority = Z_PRIORITY_DATA;
    z_reliability_t reliability = Z_RELIABILITY_BEST_EFFORT;
};

static void receive(z_loaned_sample_t *sample, void *arg) {
    auto &state = *static_cast<ProbeState *>(arg);
    const auto congestion = z_sample_congestion_control(sample);
    const auto priority = z_sample_priority(sample);
    const auto reliability = z_sample_reliability(sample);
    if (congestion != state.congestion || priority != state.priority || reliability != state.reliability)
        ++state.mismatches;
    const int count = ++state.received;
    if (count == 1 || count == state.expected_count)
        std::printf("sample=%d congestion=%d priority=%d reliability=%d\n", count,
                    int(congestion), int(priority), int(reliability));
}

int main(int argc, char **argv) {
    if (argc != 8) {
        std::fprintf(stderr, "usage: qos_probe endpoint key count timeout_s congestion priority reliability\n");
        return 2;
    }
    ProbeState state;
    state.expected_count = std::atoi(argv[3]);
    const int timeout_seconds = std::atoi(argv[4]);
    state.congestion = z_congestion_control_t(std::atoi(argv[5]));
    state.priority = z_priority_t(std::atoi(argv[6]));
    state.reliability = z_reliability_t(std::atoi(argv[7]));

    z_owned_config_t config;
    z_config_default(&config);
    if (zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "client") != Z_OK ||
        zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, argv[1]) != Z_OK ||
        zp_config_insert(z_loan_mut(config), Z_CONFIG_MULTICAST_SCOUTING_KEY, "false") != Z_OK)
        return 3;
    z_owned_session_t session;
    if (z_open(&session, z_move(config), nullptr) != Z_OK) return 4;
    if (zp_start_read_task(z_loan_mut(session), nullptr) != Z_OK ||
        zp_start_lease_task(z_loan_mut(session), nullptr) != Z_OK)
        return 5;

    z_view_keyexpr_t key;
    if (z_view_keyexpr_from_str(&key, argv[2]) != Z_OK) return 6;
    z_owned_closure_sample_t callback;
    z_closure(&callback, receive, nullptr, &state);
    z_owned_subscriber_t subscriber;
    if (z_declare_subscriber(z_loan(session), &subscriber, z_loan(key), z_move(callback), nullptr) != Z_OK)
        return 7;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    while (state.received < state.expected_count && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::printf("received=%d mismatches=%d expected=%d\n", int(state.received),
                int(state.mismatches), state.expected_count);
    z_drop(z_move(subscriber));
    z_drop(z_move(session));
    return state.received == state.expected_count && state.mismatches == 0 ? 0 : 1;
}
