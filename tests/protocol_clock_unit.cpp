#include "../board/src/protocol_clock.hpp"

#include <cassert>
#include <cstdlib>

int main() {
    xt::configure_protocol_clock("unix_epoch");
    const auto first = xt::protocol_timestamp_us();
    const auto second = xt::protocol_timestamp_us();
    assert(first > 1577836800000000LL);
    assert(second >= first);
    // This API returns the sampled realtime clock; it must never synthesize
    // nominal 30 Hz or 10 Hz presentation timestamps.
    timespec realtime{};
    assert(clock_gettime(CLOCK_REALTIME, &realtime) == 0);
    const auto sampled = int64_t(realtime.tv_sec) * 1000000LL + realtime.tv_nsec / 1000LL;
    assert(std::llabs(sampled - second) < 1000000LL);
    return 0;
}
