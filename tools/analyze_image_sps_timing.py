# -*- coding: utf-8 -*-
"""Extract SPS/profile/level/resolution via ffprobe and analyze batch/inter-sensor timing."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from pc import active_msgs_pb2 as pb

BASE = ROOT / "evidence/dual-compare"
FFPROBE = r"C:\Users\user\AppData\Local\Microsoft\WinGet\Links\ffprobe.exe"
OUT = BASE / "sps"
OUT.mkdir(exist_ok=True)


def first_idr(dev):
    d = BASE / dev / "raw/image"
    for p in sorted(d.glob("*.bin")):
        arr = pb.ImageMsgArray.FromString(p.read_bytes())
        for img in arr.array:
            data = bytes(img.data)
            # IDR NAL type 5 present?
            i = 0
            types = []
            starts = []
            while i + 3 < len(data):
                if data[i:i+4] == b"\x00\x00\x00\x01":
                    starts.append((i, 4)); i += 4
                elif data[i:i+3] == b"\x00\x00\x01":
                    starts.append((i, 3)); i += 3
                else:
                    i += 1
            types = [data[o + l] & 0x1F for o, l in starts]
            if 5 in types:
                return p, arr, img, data
    return None


def ffprobe_h264(path):
    r = subprocess.run([FFPROBE, "-v", "quiet", "-print_format", "json", "-show_streams", str(path)],
                       capture_output=True, text=True, timeout=30)
    j = json.loads(r.stdout)
    s = j["streams"][0]
    return {k: s.get(k) for k in ("codec_name", "profile", "level", "width", "height",
                                  "pix_fmt", "has_b_frames", "is_avc")}


def timing(dev):
    img_dir = BASE / dev / "raw/image"
    pc_dir = BASE / dev / "raw/pointcloud"
    imgs = sorted(img_dir.glob("*.bin"))[:20]
    intra = []
    for p in imgs:
        arr = pb.ImageMsgArray.FromString(p.read_bytes())
        stamps = [m.header.stamp for m in arr.array]
        intra += [b - a for a, b in zip(stamps, stamps[1:])]
    pcs = sorted(pc_dir.glob("*.bin"))[:20]
    pc_stamps = []
    for p in pcs:
        m = pb.LidarPointMsgArray.FromString(p.read_bytes())
        pc_stamps.append(m.array[0].header.stamp)
    img_stamps = []
    for p in imgs:
        arr = pb.ImageMsgArray.FromString(p.read_bytes())
        img_stamps.extend(m.header.stamp for m in arr.array)
    # nearest image-to-pointcloud stamp differences
    pairs = []
    for ps in pc_stamps[:10]:
        nearest = min(abs(is_ - ps) for is_ in img_stamps)
        pairs.append(nearest)
    return {
        "intra_batch_stamp_deltas_us": intra[:12],
        "intra_batch_delta_minmax_us": [min(intra), max(intra)],
        "image_pc_nearest_stamp_diff_us": pairs,
        "image_pc_diff_max_ms": round(max(pairs) / 1000, 2),
        "image_pc_diff_avg_ms": round(sum(pairs) / len(pairs) / 1000, 2),
    }


result = {}
for dev in ("rk3562", "legacy"):
    found = first_idr(dev)
    if not found:
        result[dev] = {"error": "no IDR found"}; continue
    p, arr, img, data = found
    h264 = OUT / f"{dev}_idr.h264"
    h264.write_bytes(data)
    result[dev] = {
        "source_bin": p.name,
        "ffprobe": ffprobe_h264(h264),
        "msg_width_height": [img.width, img.height],
        "msg_format": img.format,
        "timing": timing(dev),
    }

print(json.dumps(result, indent=2, ensure_ascii=False))
(BASE / "image_sps_timing.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
