#!/usr/bin/env python3
"""Derive customer-readable point-cloud samples and statistics from raw payloads."""
import argparse
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from pc import active_msgs_pb2 as pb


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", required=True)
    parser.add_argument("--sample-points", type=int, default=10000)
    args = parser.parse_args()
    root = Path(args.package)
    payloads = sorted((root / "02_raw_capture/pointcloud_payload").glob("*.bin"))
    if not payloads:
        raise SystemExit("no pointcloud payloads found")

    point_counts, xyz_min, xyz_max = [], [None, None, None], [None, None, None]
    rgbi_min, rgbi_max, sample = None, None, []
    scaler_values, parse_errors = set(), 0
    for path in payloads:
        try:
            array = pb.LidarPointMsgArray.FromString(path.read_bytes())
            if len(array.array) != 1:
                raise ValueError("array length is not one")
            message = array.array[0]
        except Exception:
            parse_errors += 1
            continue
        scaler_values.add(message.header.scaler)
        point_counts.append(len(message.points))
        for point in message.points:
            values = (point.x, point.y, point.z)
            for axis, value in enumerate(values):
                xyz_min[axis] = value if xyz_min[axis] is None else min(xyz_min[axis], value)
                xyz_max[axis] = value if xyz_max[axis] is None else max(xyz_max[axis], value)
            rgbi_min = point.rgbi if rgbi_min is None else min(rgbi_min, point.rgbi)
            rgbi_max = point.rgbi if rgbi_max is None else max(rgbi_max, point.rgbi)
            if len(sample) < args.sample_points:
                sample.append((point.x, point.y, point.z, point.rgbi, point.ring, point.offset))

    output = root / "06_pointcloud"
    fields = ("x_mm", "y_mm", "z_mm", "x_m", "y_m", "z_m", "rgbi", "ring", "offset")
    with (output / "pointcloud_sample.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields); writer.writeheader()
        for x, y, z, rgbi, ring, offset in sample:
            writer.writerow(dict(x_mm=x, y_mm=y, z_mm=z, x_m=x / 1000, y_m=y / 1000, z_m=z / 1000,
                                 rgbi=rgbi, ring=ring, offset=offset))
    with (output / "pointcloud_sample.ply").open("w", encoding="ascii", newline="\n") as handle:
        handle.write("ply\nformat ascii 1.0\n")
        handle.write("comment Source: exact protobuf pointcloud payload sample\n")
        handle.write(f"element vertex {len(sample)}\nproperty float x\nproperty float y\nproperty float z\n")
        handle.write("property uchar intensity\nend_header\n")
        for x, y, z, rgbi, _, _ in sample:
            handle.write(f"{x / 1000:.3f} {y / 1000:.3f} {z / 1000:.3f} {rgbi}\n")
    statistics = {
        "source": "02_raw_capture/pointcloud_payload/*.bin",
        "payload_files": len(payloads), "protobuf_parse_errors": parse_errors,
        "scaler_values": sorted(scaler_values), "coordinate_unit": "millimetres encoded; metres derived by /1000",
        "point_count": {"min": min(point_counts), "max": max(point_counts), "average": sum(point_counts) / len(point_counts)},
        "xyz_mm": {"min": dict(x=xyz_min[0], y=xyz_min[1], z=xyz_min[2]), "max": dict(x=xyz_max[0], y=xyz_max[1], z=xyz_max[2])},
        "rgbi": {"min": rgbi_min, "max": rgbi_max}, "sample_points": len(sample)
    }
    (output / "pointcloud_statistics.json").write_text(json.dumps(statistics, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(statistics, indent=2))


if __name__ == "__main__":
    main()
