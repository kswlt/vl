#!/usr/bin/env python3
"""Analyze real Header.stamp values from a live_acceptance_capture directory."""
from __future__ import annotations

import argparse
import bisect
import csv
import json
import statistics
from pathlib import Path


def percentile(values: list[int], pct: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    rank = (len(ordered) - 1) * pct / 100
    lo = int(rank)
    hi = min(lo + 1, len(ordered) - 1)
    return round(ordered[lo] + (ordered[hi] - ordered[lo]) * (rank - lo), 3)


def analyze(rows: list[dict[str, int]]) -> dict:
    stamps = [r["stamp"] for r in rows]
    seqs = [r["seq"] for r in rows]
    intervals = [b - a for a, b in zip(stamps, stamps[1:])]
    return {
        "count": len(rows),
        "first_stamp_us": stamps[0] if stamps else None,
        "last_stamp_us": stamps[-1] if stamps else None,
        "rollback_count": sum(delta < 0 for delta in intervals),
        "non_increasing_count": sum(delta <= 0 for delta in intervals),
        "seq_gap_count": sum(max(0, b - a - 1) for a, b in zip(seqs, seqs[1:])),
        "interval_us": {
            "min": min(intervals) if intervals else None,
            "mean": round(statistics.mean(intervals), 3) if intervals else None,
            "p50": percentile(intervals, 50), "p95": percentile(intervals, 95),
            "p99": percentile(intervals, 99), "max": max(intervals) if intervals else None,
        },
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("capture", type=Path)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--min-seconds", type=float, default=60)
    args = ap.parse_args()
    with (args.capture / "dual_stream_600s.csv").open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    streams = {kind: [{"stamp": int(r["stamp"]), "seq": int(r["seq"]),
                       "receive_unix_us": int(r["receive_unix_us"])}
                      for r in rows if r["stream"] == kind]
               for kind in ("image", "pointcloud")}
    image = streams["image"]
    cloud = streams["pointcloud"]
    errors = []
    for kind, items in streams.items():
        if len(items) < 2:
            errors.append(f"{kind}: too few frames")
    capture_span = (max(r["receive_unix_us"] for r in image + cloud) -
                    min(r["receive_unix_us"] for r in image + cloud)) / 1e6 if image or cloud else 0
    if capture_span < args.min_seconds:
        errors.append(f"capture span {capture_span:.3f}s < {args.min_seconds}s")
    results = {kind: analyze(items) for kind, items in streams.items()}
    for kind, item in results.items():
        if item["rollback_count"] or item["non_increasing_count"]:
            errors.append(f"{kind}: timestamp rollback or repeat")
        if item["first_stamp_us"] and item["first_stamp_us"] < 1577836800000000:
            errors.append(f"{kind}: stamp is not Unix epoch microseconds")
    cloud_stamps = sorted(r["stamp"] for r in cloud)
    nearest = []
    overlap_nearest = []
    if cloud_stamps:
        for row in image:
            stamp = row["stamp"]
            pos = bisect.bisect_left(cloud_stamps, stamp)
            candidates = cloud_stamps[max(0, pos - 1): min(len(cloud_stamps), pos + 1)]
            delta = min(abs(stamp - c) for c in candidates)
            nearest.append(delta)
            if cloud_stamps[0] <= stamp <= cloud_stamps[-1]:
                overlap_nearest.append(delta)
    paired = sum(delta <= 100000 for delta in nearest)
    results["nearest_image_cloud"] = {
        "image_count": len(nearest), "delta_us": {
            "p50": percentile(nearest, 50), "p90": percentile(nearest, 90),
            "p95": percentile(nearest, 95), "p99": percentile(nearest, 99),
            "max": max(nearest) if nearest else None},
        "over_100ms_count": len(nearest) - paired,
        "pair_success_rate": round(paired / len(nearest), 6) if nearest else 0,
        "overlap_image_count": len(overlap_nearest),
        "overlap_max_delta_us": max(overlap_nearest) if overlap_nearest else None,
        "overlap_over_100ms_count": sum(delta > 100000 for delta in overlap_nearest),
    }
    if any(delta > 100000 for delta in overlap_nearest):
        errors.append("image-cloud pairing exceeded 100ms inside common time span")
    results["capture_span_s"] = round(capture_span, 3)
    results["status"] = "PASS" if not errors else "FAIL"
    results["errors"] = errors
    rendered = json.dumps(results, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
