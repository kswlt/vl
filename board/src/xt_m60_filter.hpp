#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace xt_m60 {

enum class Reject : uint8_t {
    valid, zero_distance, invalid_distance, invalid_amplitude,
    below_min_amp, corner, flying_pixel, isolated_weak_pixel
};

struct Counts {
    uint32_t raw = 0, valid = 0, zero_distance = 0, invalid_distance = 0;
    uint32_t invalid_amplitude = 0, below_min_amp = 0, corner = 0;
    uint32_t flying_pixel = 0, isolated_weak_pixel = 0;
};

inline Counts classify(const std::vector<uint32_t> &dist,
                       const std::vector<uint16_t> &amp,
                       const std::vector<uint8_t> &corner,
                       unsigned width, unsigned height, uint16_t min_amp,
                       std::vector<Reject> &reason) {
    const size_t n = size_t(width) * height;
    Counts c;
    c.raw = static_cast<uint32_t>(n);
    reason.assign(n, Reject::valid);
    if (dist.size() != n || amp.size() != n || corner.size() != n) {
        c.invalid_distance = c.raw;
        std::fill(reason.begin(), reason.end(), Reject::invalid_distance);
        return c;
    }
    // Match SDK Iframe.h sentinel range and CartesianTransform's min-amplitude
    // cut. miniAmp is the sensor's reported threshold; a firmware-filtered
    // frame usually has no surviving amplitudes below it.
    for (size_t i = 0; i < n; ++i) {
        if (dist[i] == 0) reason[i] = Reject::zero_distance;
        else if (dist[i] >= 964000) reason[i] = Reject::invalid_distance;
        else if (amp[i] >= 64000) reason[i] = Reject::invalid_amplitude;
        else if (min_amp && amp[i] < min_amp) reason[i] = Reject::below_min_amp;
        else if (corner[i]) reason[i] = Reject::corner;
    }
    const std::vector<Reject> base_reason = reason;
    // The vendor's optional edge filter lives in a closed library. Restrict
    // this additional pass to weak, unsupported mixed-depth returns; retain
    // ordinary depth edges and strong isolated returns. Never change XYZ.
    for (unsigned y = 1; y + 1 < height; ++y) {
        for (unsigned x = 1; x + 1 < width; ++x) {
            const size_t i = size_t(y) * width + x;
            if (base_reason[i] != Reject::valid) continue;
            std::array<uint16_t, 8> neighbors_amp{};
            unsigned samples = 0, near = 0, far = 0, same = 0;
            const uint32_t gap = std::max<uint32_t>(180, dist[i] * 3 / 100);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const size_t j = size_t(int(y) + dy) * width + size_t(int(x) + dx);
                    if (base_reason[j] != Reject::valid) continue;
                    neighbors_amp[samples++] = amp[j];
                    if (uint64_t(dist[j]) + gap < dist[i]) ++near;
                    else if (uint64_t(dist[i]) + gap < dist[j]) ++far;
                    else ++same;
                }
            }
            if (samples < 5) continue;
            std::sort(neighbors_amp.begin(), neighbors_amp.begin() + samples);
            if (uint32_t(amp[i]) * 2 >= neighbors_amp[samples / 2]) continue;
            if (near >= 2 && far >= 2) reason[i] = Reject::flying_pixel;
            else if (!same) reason[i] = Reject::isolated_weak_pixel;
        }
    }
    for (Reject r : reason) {
        switch (r) {
        case Reject::valid: ++c.valid; break;
        case Reject::zero_distance: ++c.zero_distance; break;
        case Reject::invalid_distance: ++c.invalid_distance; break;
        case Reject::invalid_amplitude: ++c.invalid_amplitude; break;
        case Reject::below_min_amp: ++c.below_min_amp; break;
        case Reject::corner: ++c.corner; break;
        case Reject::flying_pixel: ++c.flying_pixel; break;
        case Reject::isolated_weak_pixel: ++c.isolated_weak_pixel; break;
        }
    }
    return c;
}

} // namespace xt_m60
