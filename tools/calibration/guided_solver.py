"""Camera-guided, local-only LiDAR board association and extrinsic solve.

The old radius/size-only RANSAC often chose a nearby cabinet or wall. Here each
LiDAR candidate must occupy the board volume inferred independently from the
ArUco camera pose and match its normal. The axis hint matches the published
X-forward/Y-left/Z-up cloud (including the current installation) and is only used to associate points;
the six-degree-of-freedom transform is then estimated from the captures.
"""
from __future__ import annotations

import json
import time
from pathlib import Path

import cv2
import numpy as np
from scipy.optimize import least_squares


AXIS_HINT = np.asarray([[0, -1, 0], [0, 0, -1], [1, 0, 0]], np.float64)
# Historical mechanical dimensions are an association seed, not an accepted extrinsic.
OFFSET_HINT = np.asarray([0.0, -0.07511, -0.0189], np.float64)
BOARD_CENTER = np.asarray([0.7, 0.5, 0.0], np.float64)


def _plane_for_pose(points, board_rotation, board_translation, rotation_hint,
                    translation_hint, seed, max_samples=5000):
    points = np.asarray(points, np.float64).reshape(-1, 3)
    points = points[np.isfinite(points).all(axis=1)]
    radii = np.linalg.norm(points, axis=1)
    points = points[(points[:, 0] > 0.1) & (points[:, 0] < 8.0)
                    & (radii > 0.2) & (radii < 10.0)]
    camera_guess = points @ rotation_hint.T + translation_hint
    board_guess = (camera_guess - board_translation) @ board_rotation
    # Margin absorbs the initial unknown sensor offset and slight mounting tilt.
    roi = ((board_guess[:, 0] > -0.2) & (board_guess[:, 0] < 1.6)
           & (board_guess[:, 1] > -0.2) & (board_guess[:, 1] < 1.2)
           & (np.abs(board_guess[:, 2]) < 0.65))
    crop = points[roi]
    if len(crop) < 200:
        return None
    rng = np.random.default_rng(seed)
    work = crop if len(crop) <= max_samples else crop[rng.choice(len(crop), max_samples, False)]
    expected_normal = rotation_hint.T @ board_rotation[:, 2]
    expected_center = board_rotation @ BOARD_CENTER + board_translation
    best = None
    for _ in range(2500):
        a, b, c = work[rng.choice(len(work), 3, False)]
        normal = np.cross(b-a, c-a)
        magnitude = np.linalg.norm(normal)
        if magnitude < 1e-8:
            continue
        normal /= magnitude
        if normal @ expected_normal < 0:
            normal = -normal
        alignment = float(normal @ expected_normal)
        if alignment < 0.8:
            continue
        offset = float(normal @ a)
        membership = np.abs(work @ normal - offset) < 0.025
        count = int(np.count_nonzero(membership))
        if count < 50:
            continue
        center = np.median(work[membership], axis=0)
        center_gap = float(np.linalg.norm(rotation_hint @ center + translation_hint
                                          - expected_center))
        score = count * np.exp(-(center_gap / 0.65) ** 2)
        if best is None or score > best[0]:
            best = (score, normal, offset, alignment, center_gap)
    if best is None:
        return None
    _, normal, offset, _, _ = best
    selected = work[np.abs(work @ normal - offset) < 0.025]
    center = selected.mean(axis=0)
    _, _, basis = np.linalg.svd(selected-center, full_matrices=False)
    normal = basis[-1]
    if normal @ expected_normal < 0:
        normal = -normal
    offset = float(normal @ center)
    inliers = crop[np.abs(crop @ normal - offset) < 0.025]
    if len(inliers) < 200:
        return None
    center = inliers.mean(axis=0)
    _, _, basis = np.linalg.svd(inliers-center, full_matrices=False)
    normal = basis[-1]
    if normal @ expected_normal < 0:
        normal = -normal
    offset = float(normal @ center)
    inliers = crop[np.abs(crop @ normal - offset) < 0.025]
    median_center = np.median(inliers, axis=0)
    alignment = float(normal @ expected_normal)
    center_gap = float(np.linalg.norm(rotation_hint @ median_center
                                      + translation_hint - expected_center))
    return {"normal": normal, "offset": offset, "center": median_center,
            "inliers": inliers, "crop_count": len(crop), "count": len(inliers),
            "alignment_to_hint": alignment, "center_gap_to_hint_m": center_gap,
            "plane_rms_m": float(np.sqrt(np.mean((inliers @ normal-offset)**2)))}


def _kabsch_normal_initial(pairs):
    lidar_normals = np.asarray([p["plane"]["normal"] for p in pairs])
    camera_normals = np.asarray([p["camera_normal"] for p in pairs])
    # For row vectors, lidar_normals @ R.T = camera_normals.
    u, _, vt = np.linalg.svd(lidar_normals.T @ camera_normals)
    correction = np.diag([1.0, 1.0, np.linalg.det(u @ vt)])
    return (u @ correction @ vt).T


def _translation_from_planes(pairs, rotation):
    rows, rhs = [], []
    for pair in pairs:
        n_cam = rotation @ pair["plane"]["normal"]
        rows.append(n_cam)
        rhs.append(float(n_cam @ pair["camera_board_translation"]
                        - pair["plane"]["offset"]))
    return np.linalg.lstsq(np.asarray(rows), np.asarray(rhs), rcond=None)[0]


def _normal_coverage(pairs):
    normals = np.asarray([p["camera_normal"] for p in pairs], dtype=np.float64)
    return np.linalg.svd(normals, compute_uv=False)


def _joint_fit(pairs, initial_rotation, baseline_m=0.025):
    base_rotation = initial_rotation
    selected_points = []
    for i, pair in enumerate(pairs):
        points = pair["plane"]["inliers"]
        if len(points) > 200:
            points = points[np.random.default_rng(100+i).choice(len(points), 200, False)]
        selected_points.append(points)

    def residuals(params):
        delta_rotation, _ = cv2.Rodrigues(params[:3])
        rotation = delta_rotation @ base_rotation
        translation = params[3:6]
        values = []
        for pair, points in zip(pairs, selected_points):
            normal = pair["camera_normal"]
            offset = float(normal @ pair["camera_board_translation"])
            plane_error = (points @ rotation.T + translation) @ normal - offset
            values.extend(plane_error / 0.02)
            normal_error = rotation @ pair["plane"]["normal"] - normal
            values.extend(normal_error / 0.03)
        # Physical layout says camera and LiDAR origins are only a few cm apart.
        # This is a magnitude prior; direction still comes from sufficiently
        # varied, synchronized board planes.
        values.append((np.linalg.norm(translation) - baseline_m) / 0.005)
        return np.asarray(values)

    translation0 = _translation_from_planes(pairs, base_rotation)
    result = least_squares(residuals, np.r_[np.zeros(3), translation0],
                           loss="soft_l1", f_scale=1.0, max_nfev=300)
    delta_rotation, _ = cv2.Rodrigues(result.x[:3])
    return delta_rotation @ base_rotation, result.x[3:6]


def _pose_metrics(pair, rotation, translation):
    plane = pair["plane"]
    normal_angle = float(np.degrees(np.arccos(np.clip(
        (rotation @ plane["normal"]) @ pair["camera_normal"], -1.0, 1.0))))
    camera_points = plane["inliers"] @ rotation.T + translation
    board_points = (camera_points-pair["camera_board_translation"]) @ pair["camera_board_rotation"]
    inside = ((board_points[:, 0] >= 0) & (board_points[:, 0] <= 1.4)
              & (board_points[:, 1] >= 0) & (board_points[:, 1] <= 1.0))
    depth = board_points[:, 2]
    return {"pose": pair["pose"], "lidar_inliers": plane["count"],
            "lidar_candidate_normal_alignment": plane["alignment_to_hint"],
            "lidar_plane_fit_rms_m": plane["plane_rms_m"],
            "normal_error_deg": normal_angle,
            "board_inside_fraction": float(np.mean(inside)),
            "board_plane_error_median_m": float(np.median(depth)),
            "board_plane_error_rms_m": float(np.sqrt(np.mean(depth**2)))}


def _flatten_cloud_metadata(value):
    if isinstance(value, dict):
        yield value
    elif isinstance(value, list):
        for item in value:
            yield from _flatten_cloud_metadata(item)


def _load_synchronized_points(pose_path, max_pair_delta_ms):
    """Load only the LiDAR scan nearest the camera frame's sensor timestamp.

    Legacy captures stored the last second as one concatenated array but kept
    per-scan point counts and timestamps in meta.json, so they can be recovered.
    """
    meta_path = pose_path / "meta.json"
    if not meta_path.is_file():
        return None, None, "missing_capture_metadata"
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    image_stamp = int(meta.get("image", {}).get("stamp", 0))
    scans = list(_flatten_cloud_metadata(meta.get("pointcloud_samples", [])))
    if image_stamp <= 0 or not scans:
        return None, None, "missing_sensor_timestamps"
    with np.load(pose_path / "points.npz") as data:
        all_points = data["points"]
    counts = [int(scan.get("points", 0)) for scan in scans]
    if any(n < 0 for n in counts) or sum(counts) != len(all_points):
        # New captures contain one scan. Their metadata may be wrapped in a
        # single callback list; this branch also accepts future flat layouts.
        if len(scans) == 1 and counts[0] == len(all_points):
            pass
        else:
            return None, None, "scan_metadata_point_count_mismatch"
    stamps = np.asarray([int(scan.get("stamp", 0)) for scan in scans], dtype=np.int64)
    valid = stamps > 0
    if not np.any(valid):
        return None, None, "missing_lidar_timestamps"
    indexes = np.flatnonzero(valid)
    index = int(indexes[np.argmin(np.abs(stamps[valid] - image_stamp))])
    delta_us = int(stamps[index] - image_stamp)
    if abs(delta_us) > max_pair_delta_ms * 1000:
        return None, delta_us / 1000.0, "camera_lidar_time_delta_exceeded"
    start = sum(counts[:index])
    return all_points[start:start + counts[index]], delta_us / 1000.0, None


def solve(args, collect_image_observations):
    dataset = args.dataset.resolve()
    observations, image_size = collect_image_observations(dataset)
    if len(observations) < args.min_poses:
        raise RuntimeError(f"有效图像姿态 {len(observations)} 少于要求 {args.min_poses}")
    rms, matrix, distortion, rvecs, tvecs = cv2.calibrateCamera(
        [o["object"] for o in observations], [o["pixels"] for o in observations],
        image_size, None, None)
    distortion = distortion.reshape(-1)
    pairs, rejected = [], []
    for i, observation in enumerate(observations):
        pose = observation["path"].name
        rotation_board, _ = cv2.Rodrigues(rvecs[i])
        translation_board = tvecs[i].reshape(3)
        normal_cam = rotation_board[:, 2]
        cloud_path = observation["path"] / "points.npz"
        if not cloud_path.is_file():
            rejected.append({"pose": pose, "reason": "no_pointcloud"})
            continue
        points, pair_delta_ms, pair_error = _load_synchronized_points(
            observation["path"], args.max_pair_delta_ms)
        if pair_error:
            rejected.append({"pose": pose, "reason": pair_error,
                             "pair_delta_ms": pair_delta_ms})
            continue
        # Captures made before the board-side Y-axis correction are mirrored.
        # Repair only an explicitly identified legacy dataset in memory; never
        # overwrite the original scan or silently change the current protocol.
        if getattr(args, "legacy_mirrored_y", False):
            points = points.copy()
            points[:, 1] *= -1
        plane = _plane_for_pose(points, rotation_board, translation_board,
                                AXIS_HINT, OFFSET_HINT, seed=71+i)
        if plane is None or plane["alignment_to_hint"] < 0.92:
            rejected.append({"pose": pose, "reason": "board_plane_not_isolated",
                             "alignment": None if plane is None else plane["alignment_to_hint"]})
            continue
        pairs.append({"pose": pose, "observation": observation,
                      "camera_board_rotation": rotation_board,
                      "camera_board_translation": translation_board,
                      "camera_normal": normal_cam, "pair_delta_ms": pair_delta_ms,
                      "plane": plane})
    if len(pairs) < args.min_poses:
        raise RuntimeError(f"时间同步并选出板面后仅 {len(pairs)} 组可用，至少需要 {args.min_poses}；"
                           "检查图像/雷达时间戳并重新采集")

    # A plane's sample centroid is not its geometric center: board cutouts and
    # partial field-of-view coverage bias it by decimeters. Estimate from plane
    # normals and signed plane offsets instead.
    inliers = pairs
    for _ in range(2):
        initial_rotation = _kabsch_normal_initial(inliers)
        rotation, translation = _joint_fit(inliers, initial_rotation, args.baseline_m)
        metrics = [_pose_metrics(p, rotation, translation) for p in inliers]
        retained = [p for p, m in zip(inliers, metrics)
                    if m["normal_error_deg"] <= 6.0
                    and m["board_plane_error_rms_m"] <= 0.08
                    and m["board_inside_fraction"] >= 0.70]
        if len(retained) < args.min_poses or len(retained) == len(inliers):
            break
        retained_names = {p["pose"] for p in retained}
        for p in inliers:
            if p["pose"] not in retained_names:
                rejected.append({"pose": p["pose"], "reason": "inconsistent_camera_lidar_pose"})
        inliers = retained
    initial_rotation = _kabsch_normal_initial(inliers)
    rotation, translation = _joint_fit(inliers, initial_rotation, args.baseline_m)

    # Every fifth matched pose is withheld and solved again for an independent check.
    holdout = [p for j, p in enumerate(inliers) if j % 5 == 0]
    training = [p for j, p in enumerate(inliers) if j % 5 != 0]
    if len(training) < 8:
        raise RuntimeError("留出验证后训练姿态不足")
    validation_rotation, validation_translation = _joint_fit(
        training, _kabsch_normal_initial(training), args.baseline_m)
    holdout_metrics = [_pose_metrics(p, validation_rotation, validation_translation)
                       for p in holdout]
    final_metrics = [_pose_metrics(p, rotation, translation) for p in inliers]
    plane_rms = float(np.sqrt(np.mean([m["board_plane_error_rms_m"]**2
                                       for m in final_metrics])))
    normal_rms = float(np.sqrt(np.mean([m["normal_error_deg"]**2
                                        for m in final_metrics])))
    holdout_plane_rms = float(np.sqrt(np.mean([m["board_plane_error_rms_m"]**2
                                               for m in holdout_metrics])))
    holdout_inside_min = float(min(m["board_inside_fraction"] for m in holdout_metrics))
    holdout_rotation_delta = float(np.degrees(np.arccos(np.clip(
        (np.trace(rotation @ validation_rotation.T)-1.0)/2.0, -1.0, 1.0))))
    holdout_translation_delta = float(np.linalg.norm(translation-validation_translation))
    coverage = _normal_coverage(inliers)
    coverage_ratio = float(coverage[-1] / max(coverage[0], 1e-12))
    baseline_norm = float(np.linalg.norm(translation))
    checks = {
        "enough_pose_pairs": len(inliers) >= args.min_poses,
        "intrinsic_rms_below_1_5px": rms < 1.5,
        "board_normal_coverage_ratio_at_least_0_05": coverage_ratio >= 0.05,
        "baseline_within_physical_limit": baseline_norm <= args.max_baseline_m,
        "baseline_matches_measured_magnitude": abs(baseline_norm - args.baseline_m) <= 0.015,
        "plane_rms_below_5cm": plane_rms < 0.05,
        "normal_rms_below_3deg": normal_rms < 3.0,
        "holdout_plane_rms_below_6cm": holdout_plane_rms < 0.06,
        "holdout_board_inside_above_80pct": holdout_inside_min > 0.80,
        "holdout_rotation_delta_below_1_5deg": holdout_rotation_delta < 1.5,
        "holdout_translation_delta_below_3cm": holdout_translation_delta < 0.03,
    }
    validation_pass = all(checks.values())

    output = args.output.resolve() if args.output else dataset / "result_guided"
    output.mkdir(parents=True, exist_ok=True)
    diagnostics = output / "diagnostics"
    diagnostics.mkdir(exist_ok=True)
    for pair in pairs:
        image = pair["observation"]["image"].copy()
        pts = pair["plane"]["inliers"]
        camera_points = pts @ rotation.T + translation
        camera_points = camera_points[camera_points[:, 2] > 0.1]
        pixels, _ = cv2.projectPoints(camera_points, np.zeros(3), np.zeros(3),
                                      matrix, distortion)
        for x, y in pixels.reshape(-1, 2)[::max(1, len(pixels)//3000)]:
            if 0 <= x < image.shape[1] and 0 <= y < image.shape[0]:
                cv2.circle(image, (int(round(x)), int(round(y))), 2, (255, 80, 0), -1)
        cv2.imwrite(str(diagnostics / (pair["pose"] + "_overlay.jpg")), image)

    coeff = distortion.tolist()
    camera = {"model": "MC800S", "width": image_size[0], "height": image_size[1],
              "fx": float(matrix[0, 0]), "fy": float(matrix[1, 1]),
              "cx": float(matrix[0, 2]), "cy": float(matrix[1, 2]),
              "distortion": dict(zip(("k1", "k2", "p1", "p2", "k3"), coeff[:5]))}
    result = {"device_sn": args.sn, "calibration_status": "candidate_review_required",
              "valid": False, "solver_validation_pass": bool(validation_pass),
              "legacy_pointcloud_y_corrected_in_memory": bool(getattr(args, "legacy_mirrored_y", False)),
              "solver_validation_checks": checks,
              "physical_calibration_performed": (dataset / "capture.jsonl").is_file(),
              "version": time.strftime("cal-guided-%Y%m%d-%H%M%S"),
              "camera": camera,
              "lidar_to_camera": {"R": rotation.tolist(), "t": translation.tolist(),
                                  "unit": "m", "transform": "p_camera = R * p_lidar + t"}}
    report = {"status": "SOLVER_CHECK_PASS" if validation_pass else "SOLVER_CHECK_FAIL",
              "dataset": str(dataset), "valid_image_views": len(observations),
              "legacy_pointcloud_y_corrected_in_memory": bool(getattr(args, "legacy_mirrored_y", False)),
              "matched_lidar_views": len(pairs), "used_views": len(inliers),
              "rejected_views": rejected, "intrinsic_rms_px": float(rms),
              "normal_rms_deg": normal_rms,
              "board_plane_rms_m": plane_rms,
              "pair_time_delta_ms": [p["pair_delta_ms"] for p in pairs],
              "max_pair_time_delta_ms": float(args.max_pair_delta_ms),
              "board_normal_coverage_singular_values": coverage.tolist(),
              "board_normal_coverage_ratio": coverage_ratio,
              "translation_observable": bool(coverage_ratio >= 0.05),
              "baseline_norm_m": baseline_norm,
              "baseline_prior_m": float(args.baseline_m),
              "baseline_max_m": float(args.max_baseline_m),
              "validation_checks": checks,
              "holdout_views": [p["pose"] for p in holdout],
              "holdout_plane_rms_m": holdout_plane_rms,
              "holdout_board_inside_min": holdout_inside_min,
              "holdout_rotation_delta_deg": holdout_rotation_delta,
              "holdout_translation_delta_m": holdout_translation_delta,
              "rotation_det": float(np.linalg.det(rotation)),
              "rotation_orthogonality_error": float(np.linalg.norm(rotation.T@rotation-np.eye(3))),
              "poses": final_metrics, "holdout_metrics": holdout_metrics,
              "warning": "Local candidate. Inspect overlays and physical sensor layout before deploying."}
    (output / "calibration_candidate.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    (output / "camera_intrinsics.json").write_text(
        json.dumps({"device_sn": args.sn, "resolution": list(image_size),
                    "camera": camera, "rms_reprojection_px": float(rms),
                    "valid": False}, indent=2, ensure_ascii=False), encoding="utf-8")
    (output / "lidar_camera_extrinsics.json").write_text(
        json.dumps({"device_sn": args.sn, "lidar_to_camera": result["lidar_to_camera"],
                    "solver_validation_pass": bool(validation_pass), "valid": False},
                   indent=2, ensure_ascii=False), encoding="utf-8")
    (output / "report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("status", "valid_image_views", "matched_lidar_views",
        "used_views", "intrinsic_rms_px", "normal_rms_deg", "board_plane_rms_m",
        "baseline_norm_m", "board_normal_coverage_ratio", "validation_checks",
        "holdout_plane_rms_m")},
        indent=2, ensure_ascii=False))
    print(f"本地候选结果：{output}")
