import json
import tempfile
import unittest
from pathlib import Path

import cv2
import numpy as np

from tools.calibration.guided_solver import (
    _kabsch_normal_initial,
    _load_synchronized_points,
    _normal_coverage,
    _translation_from_planes,
)


class CalibrationSolverTests(unittest.TestCase):
    def test_legacy_capture_selects_nearest_timestamp_scan_only(self):
        with tempfile.TemporaryDirectory() as temp:
            pose = Path(temp)
            scans = [
                {"stamp": 1_000_000, "points": 2, "seq": 1},
                {"stamp": 1_100_000, "points": 3, "seq": 2},
                {"stamp": 1_200_000, "points": 1, "seq": 3},
            ]
            (pose / "meta.json").write_text(json.dumps({
                "image": {"stamp": 1_108_000},
                "pointcloud_samples": [[scans[0]], [scans[1], scans[2]]],
            }), encoding="utf-8")
            all_points = np.arange(18, dtype=np.float32).reshape(6, 3)
            np.savez_compressed(pose / "points.npz", points=all_points)

            selected, delta_ms, error = _load_synchronized_points(pose, 50)

            self.assertIsNone(error)
            self.assertEqual(delta_ms, -8.0)
            np.testing.assert_array_equal(selected, all_points[2:5])

    def test_pair_is_rejected_when_timestamp_delta_exceeds_limit(self):
        with tempfile.TemporaryDirectory() as temp:
            pose = Path(temp)
            (pose / "meta.json").write_text(json.dumps({
                "image": {"stamp": 1_000_000},
                "pointcloud_samples": [[{"stamp": 1_300_001, "points": 1}]],
            }), encoding="utf-8")
            np.savez_compressed(pose / "points.npz", points=np.zeros((1, 3), np.float32))

            points, delta_ms, error = _load_synchronized_points(pose, 150)

            self.assertIsNone(points)
            self.assertEqual(delta_ms, 300.001)
            self.assertEqual(error, "camera_lidar_time_delta_exceeded")

    def test_plane_normals_recover_rotation_and_translation_convention(self):
        rv = np.array([0.15, -0.2, 0.35])
        rotation, _ = cv2.Rodrigues(rv)
        translation = np.array([0.010, -0.020, 0.005])
        camera_normals = np.asarray([
            [1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0],
            [1.0, 1.0, 1.0], [-1.0, 1.0, 1.0], [1.0, -1.0, 1.0],
        ], dtype=np.float64)
        camera_normals /= np.linalg.norm(camera_normals, axis=1, keepdims=True)
        lidar_normals = (rotation.T @ camera_normals.T).T
        pairs = []
        for index, (n_lidar, n_camera) in enumerate(zip(lidar_normals, camera_normals)):
            board_translation = np.array([0.3, -0.2, 2.0 + index * 0.1])
            offset = float(n_camera @ (board_translation - translation))
            pairs.append({
                "plane": {"normal": n_lidar, "offset": offset},
                "camera_normal": n_camera,
                "camera_board_translation": board_translation,
            })

        estimate_rotation = _kabsch_normal_initial(pairs)
        estimate_translation = _translation_from_planes(pairs, estimate_rotation)

        self.assertGreater(float(_normal_coverage(pairs)[-1]), 0.5)
        self.assertLess(np.degrees(np.linalg.norm(cv2.Rodrigues(
            estimate_rotation @ rotation.T)[0])), 1e-5)
        np.testing.assert_allclose(estimate_translation, translation, atol=1e-8)


if __name__ == "__main__":
    unittest.main()
