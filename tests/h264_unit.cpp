#include "../board/src/h264_annexb.hpp"
#include <cassert>
#include <cstdio>

static std::vector<uint8_t> nalu(uint8_t type, uint8_t payload) {
    return {0, 0, 0, 1, type, payload};
}

int main() {
    const std::vector<uint8_t> sps = {0,0,0,1,0x67,0x64,0x00,0x28,0xac,0xd2,0x01,
        0xe0,0x08,0x9f,0x96,0x10,0x00,0x00,0x3e,0x90,0x00,0x04,0xe3,0x40,0x40};
    uint32_t width = 0, height = 0;
    assert(h264_sps_dimensions(sps, width, height));
    assert(width == 1920 && height == 1080);

    AnnexBAccessUnitParser parser;
    std::vector<uint8_t> stream = sps;
    const auto pps = nalu(0x68, 0x80);
    const auto idr = nalu(0x65, 0x80);
    const auto p1 = nalu(0x41, 0x80);
    const auto p2 = nalu(0x41, 0x80);
    const auto aud = nalu(0x09, 0xf0);
    for (const auto *part : {&pps, &idr, &p1, &p2, &aud, &sps})
        stream.insert(stream.end(), part->begin(), part->end());
    parser.feed(stream.data(), stream.size());
    assert(parser.width() == 1920 && parser.height() == 1080);
    std::vector<uint8_t> au;
    assert(parser.pop(au));
    assert(h264_find_start_code(au, 0) == 0);
    bool has_sps = false, has_pps = false, has_idr = false;
    for (size_t at = 0; at < au.size();) {
        const size_t sc = h264_start_code_size(au, at);
        if (!sc) { ++at; continue; }
        const uint8_t type = au[at + sc] & 0x1f;
        has_sps |= type == 7; has_pps |= type == 8; has_idr |= type == 5;
        at += sc + 1;
    }
    assert(has_sps && has_pps && has_idr);
    assert(parser.pop(au));
    assert(parser.pop(au));
    assert(!parser.pop(au));
    printf("PASS: Annex-B SPS dimensions and complete access-unit framing\n");
}
