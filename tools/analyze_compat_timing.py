"""Analyze stable image cadence and first-frame-to-pointcloud compatibility phase."""
from __future__ import annotations

import argparse
import bisect
import csv
import json
import statistics
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("capture", type=Path)
args = parser.parse_args()

with (args.capture / "image-arrivals.csv").open(newline="", encoding="utf-8") as handle:
    image_rows = list(csv.DictReader(handle))
with (args.capture / "pointcloud-arrivals.csv").open(newline="", encoding="utf-8") as handle:
    point_rows = list(csv.DictReader(handle))

image_stamps = [int(row["source_stamp_ns"]) for row in image_rows]
point_stamps = [int(row["source_stamp_ns"]) for row in point_rows]
image_deltas = [right - left for left, right in zip(image_stamps, image_stamps[1:])]
batch_first = [int(row["source_stamp_ns"]) for row in image_rows if int(row["seq"]) % 3 == 0]
nearest = []
for stamp in batch_first:
    if stamp < point_stamps[0] or stamp > point_stamps[-1]:
        continue
    index = bisect.bisect_left(point_stamps, stamp)
    candidates = point_stamps[max(0, index - 1):min(len(point_stamps), index + 1)]
    if candidates:
        nearest.append(min(abs(stamp - candidate) for candidate in candidates))

result = {
    "image_frame_delta_us": sorted(set(image_deltas)),
    "image_frame_delta_jitter_us": max(image_deltas) - min(image_deltas),
    "batch_first_to_nearest_pointcloud_us": sorted(set(nearest)),
    "batch_first_to_pointcloud_mean_us": statistics.mean(nearest),
    "batch_first_to_pointcloud_max_us": max(nearest),
}
print(json.dumps(result, indent=2))
