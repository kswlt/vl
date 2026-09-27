# -*- coding: utf-8 -*-
"""Field-level comparison of two capture_device_compatibility manifests (RK3562 vs original legacy device)."""
import collections
import json
import statistics
import sys
from pathlib import Path

ROOT = Path(r"C:\Users\user\Desktop\project\evidence\dual-compare")
LABELS = ["rk3562", "legacy"]


def load(label):
    return json.loads((ROOT / label / "manifest.json").read_text(encoding="utf-8"))


def records_by_suffix(m, suffix):
    return [r for r in m["records"] if r.get("topic", "").endswith("/" + suffix)]


def summarize_image(recs):
    out = {}
    out["packets"] = len(recs)
    arr_lens = collections.Counter(r.get("array_length") for r in recs)
    out["array_length_dist"] = dict(arr_lens)
    fmts = collections.Counter()
    sizes = collections.Counter()
    scalers = collections.Counter()
    data_bytes = []
    all_nal = []
    idr_packet_positions = []
    sps_seen = pps_seen = idr_seen = 0
    seqs_per_packet = []
    stamps = []
    decode_errors = 0
    for idx, r in enumerate(recs):
        if r.get("decode") != "ImageMsgArray":
            decode_errors += 1
            continue
        for it in r["items"]:
            fmts[it["format"]] += 1
            sizes[(it["width"], it["height"])] += 1
            scalers[it["scaler"]] += 1
            data_bytes.append(it["data_bytes"])
            nals = it.get("h264_nal_types", [])
            all_nal.extend(nals)
            seqs_per_packet.append(it["seq"])
            stamps.append(it["stamp"])
        packet_types = set(t for it in r["items"] for t in it.get("h264_nal_types", []))
        if 7 in packet_types:
            sps_seen += 1
        if 8 in packet_types:
            pps_seen += 1
        if 5 in packet_types:
            idr_seen += 1
            idr_packet_positions.append(idx)
    out["format_dist"] = {str(k): v for k, v in fmts.items()}
    out["resolution_dist"] = {f"{k[0]}x{k[1]}": v for k, v in sizes.items()}
    out["header_scaler_dist"] = dict(scalers)
    out["frame_data_bytes_min_max_mean"] = [min(data_bytes), max(data_bytes), round(statistics.mean(data_bytes), 1)]
    out["nal_type_counts"] = dict(collections.Counter(all_nal))
    out["packets_with_sps"] = sps_seen
    out["packets_with_pps"] = pps_seen
    out["packets_with_idr"] = idr_seen
    if len(idr_packet_positions) > 1:
        gaps = [b - a for a, b in zip(idr_packet_positions, idr_packet_positions[1:])]
        out["idr_packet_gap_dist"] = dict(collections.Counter(gaps))
    out["first_seqs"] = seqs_per_packet[:9]
    out["last_seqs"] = seqs_per_packet[-3:]
    out["stamp_first"] = stamps[0] if stamps else None
    out["stamp_last"] = stamps[-1] if stamps else None
    if stamps:
        span_s = (stamps[-1] - stamps[0]) / 1_000_000
        out["stamp_span_s"] = round(span_s, 3)
        out["stamp_estimated_hz_by_seq"] = round(
            (seqs_per_packet[-1] - seqs_per_packet[0]) / max(span_s, 1e-9), 2
        ) if len(seqs_per_packet) > 1 else None
    out["decode_errors"] = decode_errors
    return out


def summarize_pc(recs):
    out = {}
    out["packets"] = len(recs)
    arr_lens = collections.Counter(r.get("array_length") for r in recs)
    out["array_length_dist"] = dict(arr_lens)
    scalers = collections.Counter()
    counts = []
    rings = collections.Counter()
    omin = 10**18
    omax = -(10**18)
    rgbi_min, rgbi_max = 10**18, 0
    xmin = ymin = zmin = 10**18
    xmax = ymax = zmax = -(10**18)
    seqs, stamps = [], []
    for r in recs:
        if r.get("decode") != "LidarPointMsgArray":
            continue
        for it in r["items"]:
            scalers[it["scaler"]] += 1
            counts.append(it["points"])
            for k, v in it.get("rings", {}).items():
                # JSON keys become strings
                rings[k] += v
            omin = min(omin, it["offsets_min"])
            omax = max(omax, it["offsets_max"])
            rgbi_min = min(rgbi_min, it["rgbi_min"])
            rgbi_max = max(rgbi_max, it["rgbi_max"])
            xmin = min(xmin, it["x_min"]); xmax = max(xmax, it["x_max"])
            ymin = min(ymin, it["y_min"]); ymax = max(ymax, it["y_max"])
            zmin = min(zmin, it["z_min"]); zmax = max(zmax, it["z_max"])
            seqs.append(it["seq"])
            stamps.append(it["stamp"])
    out["header_scaler_dist"] = dict(scalers)
    out["points_per_frame_min_max_mean"] = [min(counts), max(counts), round(statistics.mean(counts), 1)]
    out["points_constant"] = len(set(counts)) == 1
    out["ring_value_dist"] = dict(rings)
    out["offset_min_overall"] = omin
    out["offset_max_overall"] = omax
    out["rgbi_range"] = [rgbi_min, rgbi_max]
    out["xyz_mm_ranges"] = {"x": [xmin, xmax], "y": [ymin, ymax], "z": [zmin, zmax]}
    out["first_seqs"] = seqs[:9]
    out["last_seqs"] = seqs[-3:]
    if stamps:
        span_s = (stamps[-1] - stamps[0]) / 1_000_000
        out["stamp_span_s"] = round(span_s, 3)
        out["stamp_estimated_hz_by_seq"] = round(
            (seqs[-1] - seqs[0]) / max(span_s, 1e-9), 2
        ) if len(seqs) > 1 else None
        out["stamp_first"] = stamps[0]
        out["stamp_last"] = stamps[-1]
    return out


def summarize_imu(recs):
    if not recs:
        return {"packets": 0, "present": False}
    packets = len(recs)
    total = sum(r.get("array_length", 0) for r in recs)
    scalers = collections.Counter()
    seqs = []
    stamps = []
    sample_ranges = {"ax": [10**18, 0], "ay": [10**18, 0], "az": [10**18, 0],
                     "gx": [10**18, 0], "gy": [10**18, 0], "gz": [10**18, 0]}
    for r in recs:
        for it in r.get("items", []):
            scalers[it["scaler"]] += 1
            seqs.append(it["seq"])
            stamps.append(it["stamp"])
            for k in sample_ranges:
                v = it[k]
                sample_ranges[k][0] = min(sample_ranges[k][0], v)
                sample_ranges[k][1] = max(sample_ranges[k][1], v)
    out = {"packets": packets, "present": True, "total_samples": total,
           "array_length_dist": dict(collections.Counter(r.get("array_length") for r in recs)),
           "header_scaler_dist": dict(scalers),
           "first_seqs": seqs[:6], "sample_ranges": sample_ranges}
    if stamps:
        span_s = (stamps[-1] - stamps[0]) / 1_000_000
        out["stamp_span_s"] = round(span_s, 3)
        out["sample_hz_estimate"] = round((seqs[-1] - seqs[0] + 1) / max(span_s, 1e-9), 1)
    return out


def stamp_epoch_check(stamp):
    # Unix epoch microseconds for 2026-09-19 ~ 1.789e15; monotonic would be small
    if stamp is None:
        return None
    return {"value": stamp, "looks_like_unix_epoch_us": 1.7e15 < stamp < 1.9e15}


report = {}
for label in LABELS:
    m = load(label)
    img = summarize_image(records_by_suffix(m, "image"))
    pc = summarize_pc(records_by_suffix(m, "pointcloud"))
    imu = summarize_imu(records_by_suffix(m, "imu"))
    img["stamp_epoch_check"] = stamp_epoch_check(img.get("stamp_first"))
    pc["stamp_epoch_check"] = stamp_epoch_check(pc.get("stamp_first"))
    report[label] = {
        "capture_seconds": m["capture_seconds"],
        "topic_counts": m["topic_counts"],
        "dropped": m["dropped"],
        "setting_responses": m["query_results"].get("setting", []),
        "config_file_responses": len(m["query_results"].get("config_file", [])),
        "image": img,
        "pointcloud": pc,
        "imu": imu,
    }

out_path = ROOT / "field_comparison.json"
out_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(json.dumps(report, indent=2, ensure_ascii=False))
print(f"\nWROTE {out_path}")
