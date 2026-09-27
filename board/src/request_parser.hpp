#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace xt {
struct RequestReader {
    const uint8_t *p;
    size_t n, i = 0;
    bool varint(uint64_t &v) {
        v = 0;
        for (unsigned shift=0; shift<70 && i<n; shift+=7) {
            uint8_t b = p[i++];
            if (shift == 63 && b > 1) return false;
            v |= uint64_t(b & 127) << shift;
            if (!(b & 128)) return true;
        }
        return false;
    }
    bool skip(unsigned wire) {
        uint64_t v;
        if (wire == 0) return varint(v);
        if (wire == 2) { if (!varint(v) || v > n-i) return false; }
        else if (wire == 1) v = 8;
        else if (wire == 5) v = 4;
        else return false;
        if (v > n-i) return false;
        i += size_t(v); return true;
    }
};
inline bool parse_request(const uint8_t *p, size_t n, std::array<int32_t,6> &values, bool setting = true) {
    if (n > 4096) return false;
    RequestReader r{p,n};
    while (r.i < r.n) {
        uint64_t tag, v;
        if (!r.varint(tag) || tag >> 3 == 0 || tag >> 3 > 0x1fffffff) return false;
        unsigned field = unsigned(tag >> 3), wire = unsigned(tag & 7);
        if (field == 1) {
            if (wire != 2 || !r.varint(v) || v > r.n-r.i) return false;
            RequestReader h{p+r.i,size_t(v)};
            while (h.i<h.n) {
                uint64_t t, x;
                if (!h.varint(t) || t >> 3 == 0 || t >> 3 > 0x1fffffff) return false;
                if ((t >> 3) <= 3) {
                    if ((t & 7) != 0 || !h.varint(x) || ((t >> 3) == 1 && x > UINT32_MAX)) return false;
                } else if (!h.skip(t & 7)) return false;
            }
            r.i += size_t(v);
        } else if (setting && field >= 2 && field <= 7) {
            if (wire != 0 || !r.varint(v) || v > UINT32_MAX) return false;
            values[field-2] = int32_t(uint32_t(v >> 1) ^ (0u-uint32_t(v & 1)));
        } else if (!r.skip(wire)) return false;
    }
    return true;
}
}
