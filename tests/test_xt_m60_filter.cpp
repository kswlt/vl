#include "../board/src/xt_m60_filter.hpp"
#include <cassert>
#include <cstdio>
#include <cstdint>

int main(int argc, char **argv) {
    if (argc == 2) {
        FILE *f = fopen(argv[1], "rb");
        assert(f);
        uint16_t header[3];
        assert(fread(header,sizeof(header),1,f)==1);
        const size_t n=size_t(header[0])*header[1];
        std::vector<uint32_t> distances(n);
        std::vector<uint16_t> amplitudes(n);
        std::vector<uint8_t> corners(n);
        assert(fread(distances.data(),sizeof(uint32_t),n,f)==n);
        assert(fread(amplitudes.data(),sizeof(uint16_t),n,f)==n);
        assert(fread(corners.data(),sizeof(uint8_t),n,f)==n);
        fclose(f);
        std::vector<xt_m60::Reject> reasons;
        auto counts=xt_m60::classify(distances,amplitudes,corners,
                                      header[0],header[1],header[2],reasons);
        printf("raw=%u valid=%u invalid_distance=%u invalid_amplitude=%u below_min_amp=%u corner=%u flying=%u isolated_weak=%u\n",
               counts.raw, counts.valid, counts.invalid_distance,
               counts.invalid_amplitude,counts.below_min_amp,counts.corner,
               counts.flying_pixel,counts.isolated_weak_pixel);
        return 0;
    }
    constexpr unsigned w=7,h=7;
    std::vector<uint32_t> d(w*h, 2000);
    std::vector<uint16_t> a(w*h, 200);
    std::vector<uint8_t> corner(w*h, 0);
    std::vector<xt_m60::Reject> r;
    d[0]=0; d[1]=964001; a[2]=64007; a[3]=50; corner[4]=1;
    auto c=xt_m60::classify(d,a,corner,w,h,70,r);
    assert(c.raw==49 && c.valid==44 && c.zero_distance==1);
    assert(c.invalid_distance==1 && c.invalid_amplitude==1);
    assert(c.below_min_amp==1 && c.corner==1);
    // A strong isolated return is retained; no geometric-only deletion.
    d[3*w+3]=3000;
    c=xt_m60::classify(d,a,corner,w,h,70,r);
    assert(r[3*w+3]==xt_m60::Reject::valid);
    // Weak, unsupported isolated return is removed.
    a[3*w+3]=80;
    c=xt_m60::classify(d,a,corner,w,h,70,r);
    assert(r[3*w+3]==xt_m60::Reject::isolated_weak_pixel);
    // A weak return between distinct near and far surfaces is a flying pixel.
    d.assign(w*h,2000); a.assign(w*h,200); corner.assign(w*h,0);
    d[3*w+3]=3000; a[3*w+3]=80;
    d[2*w+2]=4000; d[2*w+3]=4000;
    d[4*w+3]=4000; d[4*w+4]=4000;
    c=xt_m60::classify(d,a,corner,w,h,70,r);
    assert(r[3*w+3]==xt_m60::Reject::flying_pixel);
    assert(c.valid+c.zero_distance+c.invalid_distance+c.invalid_amplitude+
           c.below_min_amp+c.corner+c.flying_pixel+c.isolated_weak_pixel==c.raw);
    std::puts("xt_m60_filter: OK");
}
