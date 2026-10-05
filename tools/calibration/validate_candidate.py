#!/usr/bin/env python3
"""Validate a frozen camera/LiDAR candidate on a separate capture dataset.

This reads local images and point clouds only. It never writes to the device or
changes the candidate's valid flag.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.calibration.guided_solver import (
    AXIS_HINT, OFFSET_HINT, _load_synchronized_points, _plane_for_pose,
)
from tools.calibration.local_calibrate import detector, detect_tags, tag_object_corners


def validate(candidate_path: Path, dataset: Path, legacy_mirrored_y: bool) -> dict:
    candidate = json.loads(candidate_path.read_text(encoding="utf-8"))
    camera = candidate["camera"]
    extrinsic = candidate["lidar_to_camera"]
    matrix = np.array([[camera["fx"], 0, camera["cx"]],
                       [0, camera["fy"], camera["cy"]], [0, 0, 1]], np.float64)
    distortion = np.array([camera["distortion"][k]
                           for k in ("k1", "k2", "p1", "p2", "k3")], np.float64)
    rotation = np.asarray(extrinsic["R"], np.float64)
    translation = np.asarray(extrinsic["t"], np.float64)
    aruco = detector()
    results = []
    for index, pose_path in enumerate(sorted((dataset / "poses").glob("pose_*"))):
        image = cv2.imread(str(pose_path / "image.jpg"))
        if image is None:
            continue
        tags, _ = detect_tags(image, aruco)
        if len(tags) < 3:
            results.append({"pose": pose_path.name, "error": "fewer_than_three_tags"})
            continue
        object_points = np.asarray([point for tid in sorted(tags)
                                    for point in tag_object_corners(tid)], np.float32)
        image_points = np.asarray([point for tid in sorted(tags)
                                   for point in tags[tid]], np.float32)
        success, rvec, tvec = cv2.solvePnP(object_points, image_points,
                                           matrix, distortion,
                                           flags=cv2.SOLVEPNP_ITERATIVE)
        if not success:
            results.append({"pose": pose_path.name, "error": "camera_pose_failed"})
            continue
        board_rotation, _ = cv2.Rodrigues(rvec)
        board_translation = tvec.reshape(3)
        points, delta_ms, error = _load_synchronized_points(pose_path, 150.0)
        if error:
            results.append({"pose": pose_path.name, "error": error,
                            "pair_delta_ms": delta_ms})
            continue
        if legacy_mirrored_y:
            points = points.copy()
            points[:, 1] *= -1
        plane = _plane_for_pose(points, board_rotation, board_translation,
                                AXIS_HINT, OFFSET_HINT, seed=71 + index)
        if plane is None:
            results.append({"pose": pose_path.name, "error": "board_plane_not_found"})
            continue
        normal = board_rotation[:, 2]
        transformed = plane["inliers"] @ rotation.T + translation
        residuals = transformed @ normal - float(normal @ board_translation)
        board_points = (transformed - board_translation) @ board_rotation
        inside = ((board_points[:, 0] >= 0) & (board_points[:, 0] <= 1.4)
                  & (board_points[:, 1] >= 0) & (board_points[:, 1] <= 1.0))
        predicted_normal = rotation @ plane["normal"]
        angle = float(np.degrees(np.arccos(np.clip(predicted_normal @ normal,
                                                    -1.0, 1.0))))
        results.append({
            "pose": pose_path.name,
            "pair_delta_ms": delta_ms,
            "lidar_inliers": plane["count"],
            "plane_fit_rms_m": plane["plane_rms_m"],
            "board_plane_median_residual_m": float(np.median(residuals)),
            "board_plane_rms_m": float(np.sqrt(np.mean(residuals**2))),
            "normal_error_deg": angle,
            "board_inside_fraction": float(np.mean(inside)),
        })
    measured = [r for r in results if "board_plane_rms_m" in r]
    if not measured:
        return {"dataset": str(dataset), "candidate": str(candidate_path),
                "status": "FAIL", "reason": "no_valid_pairs", "poses": results}
    plane_rms = float(np.sqrt(np.mean([r["board_plane_rms_m"]**2
                                        for r in measured])))
    normal_rms = float(np.sqrt(np.mean([r["normal_error_deg"]**2
                                         for r in measured])))
    checks = {
        "paired_fraction_at_least_70pct": len(measured) / len(results) >= 0.70,
        # The observed board-plane fit noise is about 1.3 cm. A separate
        # capture should remain within 3 cm RMS; this is a diagnostic gate,
        # not a physical sensor-origin certification.
        "external_plane_rms_below_3cm": plane_rms < 0.03,
        "normal_rms_below_3deg": normal_rms < 3.0,
        "board_inside_above_80pct": min(r["board_inside_fraction"]
                                         for r in measured) > 0.80,
    }
    return {"dataset": str(dataset), "candidate": str(candidate_path),
            "status": "PASS" if all(checks.values()) else "FAIL",
            "valid_pairs": len(measured), "total_poses": len(results),
            "plane_rms_m": plane_rms, "normal_rms_deg": normal_rms,
            "checks": checks, "poses": results}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--legacy-mirrored-y", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = validate(args.candidate, args.dataset, args.legacy_mirrored_y)
    rendered = json.dumps(result, indent=2, ensure_ascii=False)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(json.dumps({key: result[key] for key in
                      ("status", "valid_pairs", "total_poses", "plane_rms_m",
                       "normal_rms_deg", "checks") if key in result},
                     indent=2, ensure_ascii=False))
    if result["status"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
