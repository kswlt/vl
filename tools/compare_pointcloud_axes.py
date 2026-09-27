#!/usr/bin/env python3
"""Compare captured legacy and RK point-cloud coordinate distributions."""

import argparse
import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from pc import active_msgs_pb2 as pb


def load(directory: Path, frames: int, max_points: int) -> np.ndarray:
    chunks = []
    files = sorted((directory / "raw" / "pointcloud").glob("*.bin"))[:frames]
    for path in files:
        message = pb.LidarPointMsgArray.FromString(path.read_bytes())
        for item in message.array:
            if not item.points:
                continue
            points = np.fromiter(
                (value for point in item.points for value in (point.x, point.y, point.z)),
                dtype=np.float64,
            ).reshape(-1, 3)
            points = points[np.any(points != 0, axis=1)] / (item.header.scaler or 1000)
            chunks.append(points)
    if not chunks:
        raise RuntimeError(f"no points in {directory}")
    result = np.concatenate(chunks)
    if len(result) > max_points:
        result = result[:: max(1, len(result) // max_points)][:max_points]
    return result


def describe(points: np.ndarray) -> dict:
    centered = points - np.median(points, axis=0)
    covariance = np.cov(centered, rowvar=False)
    values, vectors = np.linalg.eigh(covariance)
    return {
        "points": len(points),
        "quantiles_m": {
            axis: dict(zip(("p01", "p05", "p50", "p95", "p99"), np.quantile(points[:, index], (.01, .05, .5, .95, .99)).round(4)))
            for index, axis in enumerate("xyz")
        },
        "positive_fraction": {
            axis: round(float(np.mean(points[:, index] > 0)), 4)
            for index, axis in enumerate("xyz")
        },
        "pca_variance": values.round(6).tolist(),
        "pca_axes_columns": vectors.round(6).tolist(),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--legacy", type=Path, required=True)
    parser.add_argument("--rk", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=20)
    parser.add_argument("--max-points", type=int, default=200_000)
    args = parser.parse_args()
    legacy = load(args.legacy, args.frames, args.max_points)
    rk = load(args.rk, args.frames, args.max_points)
    # Candidate optical-to-robotics basis change: forward/left/up = z/-x/y.
    transformed = np.column_stack((rk[:, 2], -rk[:, 0], rk[:, 1]))
    print(json.dumps({
        "legacy_as_published": describe(legacy),
        "rk_as_published": describe(rk),
        "rk_candidate_x_forward_y_left_z_up": describe(transformed),
        "candidate_rotation": [[0, 0, 1], [-1, 0, 0], [0, 1, 0]],
        "warning": "Distribution statistics cannot prove axis signs or sensor extrinsics; validate with a controlled target and physical calibration.",
    }, indent=2))


if __name__ == "__main__":
    main()
