#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <utility>
#include <vector>

class H264BitReader {
public:
    explicit H264BitReader(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

    bool bit(uint32_t &value) {
        if (bit_pos_ >= bytes_.size() * 8) return false;
        value = (bytes_[bit_pos_ / 8] >> (7 - bit_pos_ % 8)) & 1U;
        ++bit_pos_;
        return true;
    }

    bool bits(unsigned count, uint32_t &value) {
        if (count > 32) return false;
        value = 0;
        for (unsigned i = 0; i < count; ++i) {
            uint32_t next = 0;
            if (!bit(next)) return false;
            value = (value << 1) | next;
        }
        return true;
    }

    bool ue(uint32_t &value) {
        unsigned zeros = 0;
        uint32_t next = 0;
        while (true) {
            if (!bit(next)) return false;
            if (next) break;
            if (++zeros > 31) return false;
        }
        uint32_t suffix = 0;
        if (zeros && !bits(zeros, suffix)) return false;
        const uint64_t decoded = ((uint64_t{1} << zeros) - 1) + suffix;
        if (decoded > std::numeric_limits<uint32_t>::max()) return false;
        value = static_cast<uint32_t>(decoded);
        return true;
    }

    bool se(int32_t &value) {
        uint32_t code = 0;
        if (!ue(code)) return false;
        value = (code & 1U) ? static_cast<int32_t>((code + 1) / 2)
                            : -static_cast<int32_t>(code / 2);
        return true;
    }

private:
    std::vector<uint8_t> bytes_;
    size_t bit_pos_ = 0;
};

inline size_t h264_start_code_size(const std::vector<uint8_t> &bytes, size_t at) {
    if (at + 3 <= bytes.size() && bytes[at] == 0 && bytes[at + 1] == 0 && bytes[at + 2] == 1)
        return 3;
    if (at + 4 <= bytes.size() && bytes[at] == 0 && bytes[at + 1] == 0 &&
        bytes[at + 2] == 0 && bytes[at + 3] == 1)
        return 4;
    return 0;
}

inline size_t h264_find_start_code(const std::vector<uint8_t> &bytes, size_t from) {
    for (size_t i = from; i + 3 <= bytes.size(); ++i)
        if (h264_start_code_size(bytes, i)) return i;
    return std::string::npos;
}

inline std::vector<uint8_t> h264_rbsp(const std::vector<uint8_t> &nalu) {
    const size_t start = h264_start_code_size(nalu, 0);
    if (!start || start >= nalu.size()) return {};
    std::vector<uint8_t> rbsp;
    rbsp.reserve(nalu.size() - start - 1);
    unsigned zeros = 0;
    for (size_t i = start + 1; i < nalu.size(); ++i) {
        const uint8_t byte = nalu[i];
        if (zeros >= 2 && byte == 3) {
            zeros = 0;
            continue;
        }
        rbsp.push_back(byte);
        zeros = byte == 0 ? zeros + 1 : 0;
    }
    return rbsp;
}

inline bool h264_first_mb_is_zero(const std::vector<uint8_t> &nalu) {
    H264BitReader reader(h264_rbsp(nalu));
    uint32_t first_mb = 1;
    return reader.ue(first_mb) && first_mb == 0;
}

inline bool h264_skip_scaling_list(H264BitReader &reader, unsigned count) {
    int last = 8, next = 8;
    for (unsigned i = 0; i < count; ++i) {
        if (next != 0) {
            int32_t delta = 0;
            if (!reader.se(delta)) return false;
            next = (last + delta + 256) % 256;
        }
        last = next == 0 ? last : next;
    }
    return true;
}

inline bool h264_sps_dimensions(const std::vector<uint8_t> &nalu, uint32_t &width, uint32_t &height) {
    H264BitReader reader(h264_rbsp(nalu));
    uint32_t profile = 0, ignored = 0, chroma = 1, separate_colour_plane = 0;
    if (!reader.bits(8, profile) || !reader.bits(8, ignored) || !reader.bits(8, ignored) || !reader.ue(ignored))
        return false;
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 ||
        profile == 44 || profile == 83 || profile == 86 || profile == 118 ||
        profile == 128 || profile == 138 || profile == 139 || profile == 134 || profile == 135) {
        if (!reader.ue(chroma) || chroma > 3) return false;
        if (chroma == 3 && !reader.bit(separate_colour_plane)) return false;
        if (!reader.ue(ignored) || !reader.ue(ignored) || !reader.bit(ignored)) return false;
        uint32_t scaling = 0;
        if (!reader.bit(scaling)) return false;
        if (scaling) {
            for (unsigned i = 0; i < (chroma == 3 ? 12U : 8U); ++i) {
                uint32_t present = 0;
                if (!reader.bit(present)) return false;
                if (present && !h264_skip_scaling_list(reader, i < 6 ? 16 : 64)) return false;
            }
        }
    }
    if (!reader.ue(ignored)) return false; // log2_max_frame_num_minus4
    uint32_t pic_order_cnt_type = 0;
    if (!reader.ue(pic_order_cnt_type)) return false;
    if (pic_order_cnt_type == 0) {
        if (!reader.ue(ignored)) return false;
    } else if (pic_order_cnt_type == 1) {
        uint32_t flag = 0, cycle = 0;
        int32_t signed_ignored = 0;
        if (!reader.bit(flag) || !reader.se(signed_ignored) || !reader.se(signed_ignored) || !reader.ue(cycle))
            return false;
        for (uint32_t i = 0; i < cycle; ++i) if (!reader.se(signed_ignored)) return false;
    }
    uint32_t width_mbs = 0, height_map_units = 0, frame_mbs_only = 0;
    if (!reader.ue(ignored) || !reader.bit(ignored) || !reader.ue(width_mbs) ||
        !reader.ue(height_map_units) || !reader.bit(frame_mbs_only)) return false;
    if (!frame_mbs_only && !reader.bit(ignored)) return false;
    if (!reader.bit(ignored)) return false;
    uint32_t crop = 0, left = 0, right = 0, top = 0, bottom = 0;
    if (!reader.bit(crop)) return false;
    if (crop && (!reader.ue(left) || !reader.ue(right) || !reader.ue(top) || !reader.ue(bottom))) return false;

    const uint32_t chroma_array = separate_colour_plane ? 0 : chroma;
    const uint32_t sub_width = chroma_array == 1 || chroma_array == 2 ? 2 : 1;
    const uint32_t sub_height = chroma_array == 1 ? 2 : 1;
    const uint32_t crop_x = chroma_array == 0 ? 1 : sub_width;
    const uint32_t crop_y = (chroma_array == 0 ? 1 : sub_height) * (2 - frame_mbs_only);
    const uint64_t coded_width = uint64_t(width_mbs + 1) * 16;
    const uint64_t coded_height = uint64_t(height_map_units + 1) * 16 * (2 - frame_mbs_only);
    const uint64_t crop_width = uint64_t(left + right) * crop_x;
    const uint64_t crop_height = uint64_t(top + bottom) * crop_y;
    if (coded_width <= crop_width || coded_height <= crop_height) return false;
    width = static_cast<uint32_t>(coded_width - crop_width);
    height = static_cast<uint32_t>(coded_height - crop_height);
    return true;
}

class AnnexBAccessUnitParser {
public:
    void feed(const uint8_t *data, size_t size) {
        if (!size) return;
        if (stream_.size() + size > 16 * 1024 * 1024) reset();
        stream_.insert(stream_.end(), data, data + size);
        while (true) {
            size_t first = h264_find_start_code(stream_, 0);
            if (first == std::string::npos) {
                if (stream_.size() > 3) stream_.erase(stream_.begin(), stream_.end() - 3);
                return;
            }
            if (first) stream_.erase(stream_.begin(), stream_.begin() + first);
            const size_t start = h264_start_code_size(stream_, 0);
            const size_t next = h264_find_start_code(stream_, start);
            if (next == std::string::npos) return;
            std::vector<uint8_t> nalu(stream_.begin(), stream_.begin() + next);
            stream_.erase(stream_.begin(), stream_.begin() + next);
            consume(std::move(nalu));
        }
    }

    bool pop(std::vector<uint8_t> &access_unit) {
        if (ready_.empty()) return false;
        access_unit = std::move(ready_.front());
        ready_.pop_front();
        return true;
    }

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

    void reset() {
        stream_.clear();
        current_.clear();
        prefix_.clear();
        ready_.clear();
        current_has_vcl_ = current_has_sps_ = current_has_pps_ = false;
        prefix_has_sps_ = prefix_has_pps_ = false;
    }

private:
    static uint8_t type(const std::vector<uint8_t> &nalu) {
        const size_t start = h264_start_code_size(nalu, 0);
        return start < nalu.size() ? nalu[start] & 0x1f : 0;
    }

    static void append(std::vector<uint8_t> &to, const std::vector<uint8_t> &from) {
        to.insert(to.end(), from.begin(), from.end());
    }

    void finish_current() {
        if (current_has_vcl_ && !current_.empty()) ready_.push_back(std::move(current_));
        current_.clear();
        current_has_vcl_ = current_has_sps_ = current_has_pps_ = false;
    }

    void move_prefix_to_current() {
        current_ = std::move(prefix_);
        prefix_.clear();
        current_has_sps_ = prefix_has_sps_;
        current_has_pps_ = prefix_has_pps_;
        prefix_has_sps_ = prefix_has_pps_ = false;
    }

    void ensure_parameter_sets() {
        if ((!current_has_sps_ && !sps_.empty()) || (!current_has_pps_ && !pps_.empty())) {
            std::vector<uint8_t> with_parameters;
            if (!current_has_sps_) append(with_parameters, sps_);
            if (!current_has_pps_) append(with_parameters, pps_);
            append(with_parameters, current_);
            current_ = std::move(with_parameters);
            current_has_sps_ = current_has_sps_ || !sps_.empty();
            current_has_pps_ = current_has_pps_ || !pps_.empty();
        }
    }

    void consume(std::vector<uint8_t> nalu) {
        const uint8_t nal_type = type(nalu);
        if (nal_type == 7) {
            sps_ = nalu;
            uint32_t w = 0, h = 0;
            if (h264_sps_dimensions(nalu, w, h)) { width_ = w; height_ = h; }
        } else if (nal_type == 8) {
            pps_ = nalu;
        }

        if (nal_type == 9 && current_has_vcl_) finish_current();

        const bool vcl = nal_type >= 1 && nal_type <= 5;
        if (vcl) {
            const bool first_slice = h264_first_mb_is_zero(nalu);
            if (current_has_vcl_ && first_slice) finish_current();
            if (!current_has_vcl_) move_prefix_to_current();
            if (nal_type == 5) ensure_parameter_sets();
            append(current_, nalu);
            current_has_vcl_ = true;
            return;
        }

        if (current_has_vcl_) {
            append(prefix_, nalu);
            prefix_has_sps_ = prefix_has_sps_ || nal_type == 7;
            prefix_has_pps_ = prefix_has_pps_ || nal_type == 8;
        } else {
            append(prefix_, nalu);
            prefix_has_sps_ = prefix_has_sps_ || nal_type == 7;
            prefix_has_pps_ = prefix_has_pps_ || nal_type == 8;
        }
    }

    std::vector<uint8_t> stream_, current_, prefix_, sps_, pps_;
    std::deque<std::vector<uint8_t>> ready_;
    bool current_has_vcl_ = false, current_has_sps_ = false, current_has_pps_ = false;
    bool prefix_has_sps_ = false, prefix_has_pps_ = false;
    uint32_t width_ = 0, height_ = 0;
};
