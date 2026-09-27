#!/usr/bin/env python3
"""Check sustained source frame rates from captured protocol headers."""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("capture", type=Path)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--image-min", type=float, default=27)
    ap.add_argument("--image-max", type=float, default=33)
    ap.add_argument("--cloud-min", type=float, default=9)
    ap.add_argument("--cloud-max", type=float, default=11)
    args = ap.parse_args()
    with (args.capture / "dual_stream_600s.csv").open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    results = {}
    errors = []
    for kind, low, high in (("image", args.image_min, args.image_max),
                            ("pointcloud", args.cloud_min, args.cloud_max)):
        items = [r for r in rows if r["stream"] == kind]
        span = (int(items[-1]["receive_unix_us"]) - int(items[0]["receive_unix_us"])) / 1e6 if len(items) > 1 else 0
        rate = (len(items) - 1) / span if span > 0 else 0
        results[kind] = {"count": len(items), "arrival_span_s": round(span, 3),
                         "measured_fps": round(rate, 3), "target_fps": [low, high]}
        if not low <= rate <= high:
            errors.append(f"{kind}: {rate:.3f} Hz outside [{low}, {high}]")
    results["status"] = "PASS" if not errors else "FAIL"
    results["errors"] = errors
    rendered = json.dumps(results, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
