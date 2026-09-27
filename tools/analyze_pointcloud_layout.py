# -*- coding: utf-8 -*-
"""Compare pointcloud layout: fixed-grid vs sparse, invalid points, axes (ground plane), rgbi."""
import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from pc import active_msgs_pb2 as pb

BASE = ROOT / "evidence/dual-compare"
N_FRAMES = 30


def load_frames(dev, n):
    d = BASE / dev / "raw/pointcloud"
    out = []
    for p in sorted(d.glob("*.bin"))[:n]:
        arr = pb.LidarPointMsgArray.FromString(p.read_bytes())
        msg = arr.array[0]
        out.append(msg)
    return out


def axis_stats(pts, scale=1000.0):
    # histogram-based dominant plane: count points near each candidate plane (x=0,y=0,z=0)
    import math
    res = {}
    for ax, idx in (("x", 0), ("y", 1), ("z", 2)):
        vals = [pt.x / scale for pt in pts] if ax == "x" else ([pt.y / scale for pt in pts] if ax == "y" else [pt.z / scale for pt in pts])
        # ground plane = densest 5cm horizontal slab (excluding 0,0,0 invalid cluster)
        valid = [v for v, p in zip(vals, pts) if not (p.x == 0 and p.y == 0 and p.z == 0)]
        if not valid:
            res[ax] = None; continue
        lo, hi = min(valid), max(valid)
        width = hi - lo
        slab = max(0.02, width / 400)
        hist = collections.Counter(int(v / slab) for v in valid)
        bin_, cnt = hist.most_common(1)[0]
        res[ax] = {"range_m": [round(lo, 3), round(hi, 3)], "densest_slab_center_m": round((bin_ + 0.5) * slab, 3), "slab_fraction": round(cnt / len(valid), 3)}
    return res


def analyze(dev):
    frames = load_frames(dev, N_FRAMES)
    counts = [len(f.points) for f in frames]
    f0 = frames[0]
    pts = list(f0.points)
    zero = sum(1 for p in pts if p.x == 0 and p.y == 0 and p.z == 0)
    ring_vals = collections.Counter(p.ring for p in pts)
    offset_vals = collections.Counter(p.offset for p in pts)
    rgbi_vals = [p.rgbi for p in pts]
    # unknown fields check (raw protobuf wire)
    raw = BASE / dev / "raw/pointcloud" / "000001.bin"
    wire_unknown = sorted({(t >> 3) for t in []})  # pb2 discards unknowns; check via raw parse below
    # azimuth stepping: sort points of a frame by atan2 in xy, look at angle step regularity
    import math
    ang = sorted(math.atan2(p.y, p.x) for p in pts if not (p.x == 0 and p.y == 0 and p.z == 0))
    steps = [(b - a) % (2 * math.pi) for a, b in zip(ang, ang[1:])]
    step_counter = collections.Counter(round(s, 4) for s in steps if s > 0)
    common_steps = step_counter.most_common(5)
    # z distribution for ground-plane inference (legacy frame convention)
    ground = axis_stats(pts)
    # frame-to-frame count constancy
    return {
        "device": dev,
        "frames_checked": len(frames),
        "points_per_frame": {"min": min(counts), "max": max(counts), "constant": len(set(counts)) == 1, "counts_head": counts[:10]},
        "frame0_point_count": len(pts),
        "zero_xyz_points": zero,
        "zero_fraction": round(zero / max(1, len(pts)), 4),
        "ring_values": dict(list(ring_vals.items())[:5]),
        "offset_values": dict(list(offset_vals.items())[:5]),
        "rgbi_minmax": [min(rgbi_vals), max(rgbi_vals)],
        "rgbi_zero_fraction": round(sum(1 for v in rgbi_vals if v == 0) / len(rgbi_vals), 4),
        "azimuth_common_steps_rad": common_steps,
        "axis_slab_analysis": ground,
        "header": {"seq": f0.header.seq, "stamp": f0.header.stamp, "scaler": f0.header.scaler},
    }


out = [analyze("rk3562"), analyze("legacy")]
print(json.dumps(out, indent=2, ensure_ascii=False))
(BASE / "pointcloud_layout_analysis.json").write_text(json.dumps(out, indent=2, ensure_ascii=False), encoding="utf-8")
