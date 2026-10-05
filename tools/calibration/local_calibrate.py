#!/usr/bin/env python3
"""Local-only calibration capture and solver for the 1400 x 1000 mm board.

This tool reads ImageMsgArray/JPEG or H.264 and LidarPointMsgArray from one Zenoh
session. It never writes to or deploys files to the RK board. Solver outputs
are candidates and are deliberately marked valid=false for human review.
"""
from __future__ import annotations

import argparse
import collections
import json
import sys
import threading
import time
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from pc import active_msgs_pb2 as pb  # noqa: E402


BOARD_W = 1.400
BOARD_H = 1.000
TAG_SIZE = 0.200
TAG_ORIGINS = {
    1: (0.050, 0.050),
    2: (1.150, 0.050),
    3: (0.050, 0.750),
    4: (1.150, 0.750),
}
HOLE_CENTERS = [(0.450, 0.300), (0.950, 0.300),
                (0.450, 0.700), (0.950, 0.700)]
DICT_NAME = "DICT_6X6_250"


def detector():
    dictionary = cv2.aruco.getPredefinedDictionary(getattr(cv2.aruco, DICT_NAME))
    params = cv2.aruco.DetectorParameters()
    params.cornerRefinementMethod = cv2.aruco.CORNER_REFINE_SUBPIX
    params.minMarkerPerimeterRate = 0.02
    params.maxErroneousBitsInBorderRate = 0.50
    params.minCornerDistanceRate = 0.02
    params.minMarkerDistanceRate = 0.02
    params.polygonalApproxAccuracyRate = 0.04
    return cv2.aruco.ArucoDetector(dictionary, params)


def tag_object_corners(tag_id: int) -> np.ndarray:
    x, y = TAG_ORIGINS[tag_id]
    s = TAG_SIZE
    return np.asarray([[x, y, 0], [x+s, y, 0], [x+s, y+s, 0], [x, y+s, 0]],
                      dtype=np.float32)


def detect_tags(image: np.ndarray, aruco_detector=None):
    if aruco_detector is None:
        aruco_detector = detector()
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY) if image.ndim == 3 else image
    corners, ids, _ = aruco_detector.detectMarkers(gray)
    found: dict[int, tuple[np.ndarray, float]] = {}
    duplicates = {}
    if ids is not None:
        for polygon, raw_id in zip(corners, ids.flatten()):
            tag_id = int(raw_id)
            if tag_id not in TAG_ORIGINS:
                continue
            pts = polygon.reshape(4, 2).astype(np.float32)
            area = abs(float(cv2.contourArea(pts)))
            if tag_id in found:
                duplicates[tag_id] = duplicates.get(tag_id, 1) + 1
                if area <= found[tag_id][1]:
                    continue
            found[tag_id] = (pts, area)
    return {k: v[0] for k, v in found.items()}, duplicates


def append_jsonl(path: Path, item: dict) -> None:
    with path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(item, ensure_ascii=False) + "\n")


def quick_solve(dataset: Path, out: Path, status: dict, slock: threading.Lock) -> None:
    """Show a camera-only capture check; extrinsics require guided offline solve."""
    try:
        saved = len(list((dataset / "poses").glob("pose_*/image.jpg")))
        if saved < 8:
            with slock:
                status.update({"poses": saved, "busy": False, "msg": "内参需至少8组"})
            return
        observations, image_size = collect_image_observations(dataset)
        obj = [o["object"] for o in observations]
        img = [o["pixels"] for o in observations]
        rms, K, distortion, rvecs, tvecs = cv2.calibrateCamera(obj, img, image_size, None, None)
        with slock:
            status.update({
                "poses": len(observations), "pairs": 0,
                "rms": float(rms), "norm_rms": 0.0, "offset_rms": 0.0,
                "R": None, "t": None, "K": np.asarray(K), "dist": distortion.reshape(-1),
                "per_pose": {}, "busy": False, "msg": "离线求解外参"})
    except Exception as exc:
        with slock:
            status.update({"busy": False, "msg": f"err {type(exc).__name__}"})


def capture(args) -> None:
    import os
    os.environ["OPENCV_LOG_LEVEL"] = "OFF"
    os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = "loglevel quiet"
    import zenoh

    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        raise SystemExit(f"输出目录非空，拒绝覆盖：{out}")
    out.mkdir(parents=True, exist_ok=True)
    (out / "poses").mkdir(exist_ok=True)
    cfg = zenoh.Config()
    cfg.insert_json5("mode", '"client"')
    cfg.insert_json5("connect/endpoints", json.dumps([args.endpoint]))
    cfg.insert_json5("scouting/multicast/enabled", "false")
    lock = threading.Lock()
    latest_image = None
    latest_image_time = 0.0
    latest_image_meta = {}
    recv_times = collections.deque(maxlen=200)
    image_bad = 0
    cloud_count = 0
    # Keep individual sensor-stamped scans; never merge a moving target's
    # last second of LiDAR frames into one camera pose.
    clouds = collections.deque(maxlen=80)
    ar = detector()
    reference_matrix = reference_distortion = None
    if args.reference_camera is not None:
        reference = json.loads(args.reference_camera.read_text(encoding="utf-8"))["camera"]
        reference_matrix = np.array([[reference["fx"], 0, reference["cx"]],
                                     [0, reference["fy"], reference["cy"]],
                                     [0, 0, 1]], dtype=np.float64)
        reference_distortion = np.array(
            [reference["distortion"][key] for key in ("k1", "k2", "p1", "p2", "k3")],
            dtype=np.float64)

    def board_normal_angles(tags):
        if reference_matrix is None or len(tags) < 3:
            return None
        object_points = np.array(
            [corner for tag_id in sorted(tags) for corner in tag_object_corners(tag_id)],
            dtype=np.float32)
        image_points = np.array(
            [corner for tag_id in sorted(tags) for corner in tags[tag_id]],
            dtype=np.float32)
        ok, rotation_vector, _ = cv2.solvePnP(
            object_points, image_points, reference_matrix, reference_distortion,
            flags=cv2.SOLVEPNP_ITERATIVE)
        if not ok:
            return None
        rotation, _ = cv2.Rodrigues(rotation_vector)
        normal = rotation[:, 2]
        return (float(np.degrees(np.arctan2(normal[1], normal[2]))),
                float(np.degrees(np.arctan2(normal[0], normal[2]))))
    stop = threading.Event()
    feedback = None
    dbg_n = 0
    # H264 streaming support: keep an Annex-B stream since the last IDR.
    h264_buf = bytearray()
    h264_meta = {}
    h264_recv_time = 0.0
    h264_last_decode = 0.0
    h264_latest_frame = None
    tag_smooth = {}   # tag_id -> smoothed display corners (EMA)
    tracked_tags = {}  # tag_id -> {"pts": corners, "miss": consecutive misses}
    solve_status = {"poses": 0, "pairs": 0, "rms": 0.0, "norm_rms": 0.0, "offset_rms": 0.0,
                    "R": None, "t": None, "K": None, "dist": None, "per_pose": {},
                    "proj_lidar": None, "busy": False, "msg": ""}
    solve_lock = threading.Lock()
    solve_thread = None
    proj_cache = []
    proj_cache_time = 0.0
    last_detect = 0.0
    cached_tags = {}
    cached_dups = {}

    def track_tags(raw: dict):
        nonlocal tracked_tags
        for tid in list(tracked_tags):
            if tid not in raw:
                tracked_tags[tid]["miss"] += 1
                if tracked_tags[tid]["miss"] > 8:
                    del tracked_tags[tid]
        for tid, pts in raw.items():
            tracked_tags[tid] = {"pts": pts.astype(np.float32), "miss": 0}
        return {tid: v["pts"] for tid, v in tracked_tags.items()}

    def smooth_tags(raw: dict):
        nonlocal tag_smooth
        out = {}
        for tid, pts in raw.items():
            p = pts.astype(np.float32)
            if tid in tag_smooth:
                p = 0.5 * p + 0.5 * tag_smooth[tid]
            tag_smooth[tid] = p
            out[tid] = p
        return out

    def _frame_has_idr(data: bytes) -> bool:
        i = 0
        n = len(data)
        while i < n - 4:
            if data[i:i+4] == b"\x00\x00\x00\x01":
                if (data[i+4] & 0x1F) == 5:
                    return True
                i += 4
            else:
                i += 1
        return False

    def decode_h264_buffer(buf: bytes, retries: int = 1):
        """Decode Annex-B H264 stream (starting at an IDR) and return last frame."""
        tmp = out / "_h264_tmp.h264"
        tmp.write_bytes(buf)
        cap = cv2.VideoCapture(str(tmp))
        frame = None
        got = 0
        while True:
            ok, f = cap.read()
            if not ok:
                break
            frame = f
            got += 1
        cap.release()
        if frame is None and retries > 0:
            # Skip forward to the first IDR inside the buffer and retry once.
            i = 0
            n = len(buf)
            while i < n - 5:
                if buf[i:i+4] == b"\x00\x00\x00\x01" and (buf[i+4] & 0x1F) == 5:
                    return decode_h264_buffer(buf[i:], retries - 1)
                i += 1
        return frame

    def image_cb(sample):
        nonlocal latest_image, latest_image_time, latest_image_meta, image_bad
        nonlocal h264_buf, h264_meta, h264_recv_time
        received = time.monotonic()
        try:
            arr = pb.ImageMsgArray.FromString(sample.payload.to_bytes())
            if not arr.array:
                return
            msg = arr.array[-1]
            if msg.format == pb.ImageFormatH264:
                data = bytes(msg.data)
                with lock:
                    if _frame_has_idr(data):
                        h264_buf.clear()
                    h264_buf += data
                    if len(h264_buf) > 16_000_000:
                        del h264_buf[:len(h264_buf) - 12_000_000]
                    h264_meta = {"stamp": int(msg.header.stamp), "seq": int(msg.header.seq),
                                 "width": int(msg.width), "height": int(msg.height),
                                 "format": "h264"}
                    h264_recv_time = received
                    recv_times.append(received)
                return
            if msg.format != pb.ImageFormatJpeg:
                return
            if not msg.data.startswith(b"\xff\xd8") or not msg.data.endswith(b"\xff\xd9"):
                image_bad += 1
                return
            with lock:
                latest_image = bytes(msg.data)
                latest_image_time = received
                recv_times.append(received)
                latest_image_meta = {"stamp": int(msg.header.stamp), "seq": int(msg.header.seq),
                                     "width": int(msg.width), "height": int(msg.height),
                                     "format": "jpeg"}
        except Exception as exc:
            image_bad += 1
            print(f"[image] 丢弃无法解析的数据：{exc}", file=sys.stderr)

    def cloud_cb(sample):
        nonlocal cloud_count
        received = time.monotonic()
        try:
            arr = pb.LidarPointMsgArray.FromString(sample.payload.to_bytes())
            for msg in arr.array:
                scale = int(msg.header.scaler)
                if not scale:
                    continue
                xyz = np.fromiter((v for p in msg.points for v in
                                   (p.x / scale, p.y / scale, p.z / scale)),
                                  dtype=np.float32, count=3 * len(msg.points)).reshape(-1, 3)
                if len(xyz):
                    metadata = {"stamp": int(msg.header.stamp), "seq": int(msg.header.seq),
                                "scaler": scale, "points": len(msg.points)}
                    with lock:
                        clouds.append((received, int(msg.header.stamp), xyz, metadata))
                        cloud_count += 1
        except Exception as exc:
            print(f"[pointcloud] 丢弃无法解析的数据：{exc}", file=sys.stderr)

    image_topic = f"active/{args.sn}/image"
    cloud_topic = f"active/{args.sn}/pointcloud"
    print(f"订阅：{args.endpoint}\nSN：{args.sn}")
    print("图像源自动适配：设备输出 JPEG 或 H264 均可采集。")
    print(f'  .\\.venv\\Scripts\\python.exe tools\\acceptance\\set_image_format.py --endpoint "{args.endpoint}" --sn {args.sn} --format jpeg')
    print("每次采集前让板稳定不动至少 2 秒，再按空格；q 退出。只配对时间戳最近的一帧雷达点云。")
    print("建议采 20–30 个姿态：板面法向要明显改变，分别向左右、上下倾斜；仅平移或绕板法线旋转不够。")
    samples_index = out / "capture.jsonl"
    count = 0
    with zenoh.open(cfg) as session:
        sub_image = session.declare_subscriber(image_topic, image_cb)
        sub_cloud = session.declare_subscriber(cloud_topic, cloud_cb)
        window = "RK Local Calibration Capture"
        cv2.namedWindow(window, cv2.WINDOW_NORMAL)
        try:
            while not stop.is_set():
                with lock:
                    jpeg = latest_image
                    im_time = latest_image_time
                    im_meta = dict(latest_image_meta)
                    cloud_snapshot = list(clouds)
                    rt = list(recv_times)
                    hbuf = bytes(h264_buf)
                    hmeta = dict(h264_meta)
                    hrecv = h264_recv_time
                now = time.monotonic()
                fps = sum(1 for t in rt if now - t <= 2.0) / 2.0
                canvas = np.zeros((720, 1280, 3), dtype=np.uint8)
                image = None
                src_desc = None
                if jpeg:
                    image = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
                    src_desc = f"JPEG {fps:.1f}Hz"
                    frame_age = now - im_time
                elif hbuf:
                    # H264: decode the GOP stream at ~3 Hz for display.
                    if now - h264_last_decode >= 0.3:
                        decoded = decode_h264_buffer(hbuf)
                        if decoded is not None:
                            h264_latest_frame = decoded
                        h264_last_decode = now
                    if h264_latest_frame is not None:
                        image = h264_latest_frame
                    src_desc = f"H264 {fps:.1f}Hz (GOP decode)"
                    frame_age = now - hrecv if hrecv else -1.0
                else:
                    frame_age = -1.0
                if image is not None:
                    if now - proj_cache_time >= 0.5:
                        with solve_lock:
                            ss_R = solve_status["R"]; ss_t = solve_status["t"]
                            ss_K = solve_status["K"]; ss_d = solve_status["dist"]
                            lid = solve_status["proj_lidar"]
                        if ss_R is not None and lid is not None and len(lid):
                            cam3 = (np.asarray(ss_R) @ np.asarray(lid).T).T + np.asarray(ss_t).reshape(3)
                            vis = cam3[:, 2] > 0.1
                            if np.any(vis):
                                proj, _ = cv2.projectPoints(cam3[vis].astype(np.float64),
                                                            np.zeros(3), np.zeros(3),
                                                            np.asarray(ss_K), np.asarray(ss_d))
                                proj = proj.reshape(-1, 2)
                                ih, iw = image.shape[:2]
                                proj_cache.clear()
                                for uu, vv in proj[::max(1, len(proj) // 600)]:
                                    if 0 <= uu < iw and 0 <= vv < ih:
                                        proj_cache.append((int(uu), int(vv)))
                            else:
                                proj_cache.clear()
                        proj_cache_time = now
                    for uu, vv in proj_cache:
                        cv2.circle(image, (uu, vv), 2, (255, 140, 0), -1)
                    if now - last_detect >= 0.25 or not cached_tags:
                        tags_raw, duplicates = detect_tags(image, ar)
                        cached_tags = tags_raw
                        cached_dups = duplicates
                        last_detect = now
                    tags = smooth_tags(track_tags(cached_tags))
                    duplicates = cached_dups
                    live_angles = board_normal_angles(tags)
                    for tid, pts in tags.items():
                        cv2.polylines(image, [pts.astype(np.int32)], True, (0, 255, 0), 3)
                        cv2.putText(image, f"ID {tid}", tuple(pts[0].astype(int)),
                                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)
                    if duplicates:
                        title = f"Duplicate tag IDs: {duplicates}; remove other boards from view"
                    else:
                        title = (f"Detected IDs: {sorted(tags)} | clouds: {len(cloud_snapshot)}"
                                 f" | {src_desc} age {frame_age:.2f}s | bad {image_bad}"
                                 f" | poses={solve_status['poses']}"
                                 f" intr={solve_status['rms']:.2f}px {solve_status['msg']}")
                    h, w = image.shape[:2]
                    scale = min(1.0, 1180 / w, (580 if reference_matrix is not None else 630) / h)
                    shown = cv2.resize(image, (round(w*scale), round(h*scale)))
                    canvas[:] = 25
                    y, x = 55, (1280-shown.shape[1])//2
                    canvas[y:y+shown.shape[0], x:x+shown.shape[1]] = shown
                    dbg_shown_mean = float(shown.mean())
                    cv2.putText(canvas, title, (25, 30), cv2.FONT_HERSHEY_SIMPLEX,
                                0.65, (255, 255, 255), 2)
                    if reference_matrix is not None:
                        angle_text = ("Board pitch {:.1f} deg, yaw {:.1f} deg; "
                                      "capture BOTH pitch directions".format(*live_angles)
                                      if live_angles is not None else "Board pitch: detect 3+ tags")
                        cv2.putText(canvas, angle_text, (25, 670), cv2.FONT_HERSHEY_SIMPLEX,
                                    0.7, (0, 255, 255), 2)
                    cv2.putText(canvas, "SPACE capture pose   Q quit", (25, 710),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
                else:
                    if im_time > 0:
                        title = (f"Waiting for image ({'JPEG' if jpeg else 'H264' if hbuf else '-'})  |  "
                                 f"img {fps:.1f}Hz  |  last frame {now - im_time:.1f}s ago  |  "
                                 f"bad {image_bad}  |  clouds {len(cloud_snapshot)}")
                    else:
                        title = (f"Waiting for image (JPEG/H264)  |  img {fps:.1f}Hz  |  bad {image_bad}  |  "
                                 f"clouds {len(cloud_snapshot)}")
                    cv2.putText(canvas, title, (25, 40), cv2.FONT_HERSHEY_SIMPLEX,
                                0.65, (255, 255, 255), 2)
                    cv2.putText(canvas, "SPACE capture pose   Q quit", (25, 700),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
                if feedback and now < feedback["until"]:
                    fb_text, fb_color = feedback["text"], feedback["color"]
                    (tw, th), _ = cv2.getTextSize(fb_text, cv2.FONT_HERSHEY_SIMPLEX, 1.4, 3)
                    bx, by = (1280 - tw) // 2 - 24, 330
                    cv2.rectangle(canvas, (bx, by - 12), (bx + tw + 48, by + th + 24), (0, 0, 0), -1)
                    cv2.putText(canvas, fb_text, (bx + 24, by + th), cv2.FONT_HERSHEY_SIMPLEX,
                                1.4, fb_color, 3)
                dbg_n += 1
                if dbg_n % 60 == 0:
                    cv2.imwrite(str(out / "debug_canvas.jpg"), canvas)
                    sm = dbg_shown_mean if "dbg_shown_mean" in dir() else -1.0
                    print(f"DEBUG dbg={dbg_n} jpeg={'Y' if jpeg else 'N'} "
                          f"shown_mean={sm:.1f} canvas_mean={float(canvas.mean()):.1f}")
                cv2.imshow(window, canvas)
                key = cv2.waitKey(20) & 0xFF
                if key in (ord("q"), 27):
                    break
                if key != 32:
                    continue
                now = time.monotonic()
                fresh_jpeg = jpeg and now - im_time <= 1.0
                fresh_h264 = (not jpeg) and h264_latest_frame is not None and hrecv and now - hrecv <= 1.0
                if not fresh_jpeg and not fresh_h264:
                    feedback = {"text": "FAIL: no fresh image frame", "color": (0, 0, 255), "until": now + 2.5}
                    print("没有新鲜图像帧。确认 Zenoh 图像主题有数据（JPEG 或 H264）。")
                    continue
                if fresh_jpeg:
                    frame = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
                else:
                    with lock:
                        fresh_buf = bytes(h264_buf)
                    if fresh_buf:
                        dec = decode_h264_buffer(fresh_buf)
                        if dec is not None:
                            h264_latest_frame = dec
                    frame = h264_latest_frame
                if frame is None:
                    feedback = {"text": "FAIL: image decode error", "color": (0, 0, 255), "until": now + 2.5}
                    print("拒绝保存：图像帧解码失败。")
                    continue
                tags, duplicates = detect_tags(frame, ar)
                if len(tags) < 3:
                    feedback = {"text": f"FAIL: tags {sorted(tags)} < 3", "color": (0, 0, 255), "until": now + 2.5}
                    print(f"拒绝保存：只检测到 {sorted(tags)}，请让至少 3 个标记清晰入镜（板子稳住后稍等 1 秒再按空格）。")
                    continue
                image_stamp = int((im_meta if fresh_jpeg else hmeta).get("stamp", 0))
                stamped_clouds = [c for c in cloud_snapshot if c[1] > 0]
                nearest = min(stamped_clouds, key=lambda c: abs(c[1] - image_stamp), default=None)
                max_pair_delta_us = int(args.max_pair_delta_ms * 1000)
                if (nearest is None or image_stamp <= 0
                        or abs(nearest[1] - image_stamp) > max_pair_delta_us):
                    delta_ms = None if nearest is None or image_stamp <= 0 else abs(nearest[1] - image_stamp) / 1000
                    feedback = {"text": f"FAIL: image/LiDAR time gap {delta_ms if delta_ms is not None else 'unknown'} ms",
                                "color": (0, 0, 255), "until": now + 2.5}
                    print(f"拒绝保存：图像和雷达最近帧时间差 {delta_ms} ms，要求 ≤ {args.max_pair_delta_ms} ms；板子保持静止后重试。")
                    continue
                pts = nearest[2]
                if len(pts) < 30:
                    feedback = {"text": f"FAIL: only {len(pts)} pts", "color": (0, 0, 255), "until": now + 2.5}
                    print(f"拒绝保存：当前点云只有 {len(pts)} 点。")
                    continue
                count += 1
                pose_dir = out / "poses" / f"pose_{count:03d}"
                pose_dir.mkdir(parents=True)
                if fresh_jpeg:
                    (pose_dir / "image.jpg").write_bytes(jpeg)
                else:
                    cv2.imwrite(str(pose_dir / "image.jpg"), frame,
                                [cv2.IMWRITE_JPEG_QUALITY, 95])
                np.savez_compressed(pose_dir / "points.npz", points=pts.astype(np.float32))
                meta = {"pose": count, "board_detected_ids": sorted(tags),
                        "duplicate_ids": duplicates,
                        "image": (im_meta if fresh_jpeg else hmeta),
                        "pointcloud_samples": [[nearest[3]]],
                        "image_lidar_delta_us": int(nearest[1] - image_stamp),
                        "point_count": int(len(pts)), "points_unit": "m",
                        "capture_unix_ns": time.time_ns()}
                saved_angles = board_normal_angles(tags)
                if saved_angles is not None:
                    meta["board_normal_pitch_deg"], meta["board_normal_yaw_deg"] = saved_angles
                (pose_dir / "meta.json").write_text(json.dumps(meta, indent=2, ensure_ascii=False),
                                                     encoding="utf-8")
                append_jsonl(samples_index, meta)
                print(f"已保存 pose_{count:03d}: tags={sorted(tags)}, 点数={len(pts)}")
                with solve_lock:
                    _pl = pts if len(pts) <= 3000 else pts[np.random.default_rng().choice(
                        len(pts), 3000, replace=False)]
                    solve_status["proj_lidar"] = _pl
                if solve_thread is None or not solve_thread.is_alive():
                    solve_thread = threading.Thread(
                        target=quick_solve, args=(out, out, solve_status, solve_lock), daemon=True)
                    solve_thread.start()
                feedback = {"text": f"SAVED pose_{count:03d}  tags={sorted(tags)}  pts={len(pts)}",
                            "color": (0, 255, 0), "until": now + 2.5}
        finally:
            sub_image.undeclare()
            sub_cloud.undeclare()
            cv2.destroyAllWindows()
    print(f"采集结束：{count} 个姿态，数据在 {out}")


def collect_image_observations(dataset: Path):
    ar = detector()
    observations = []
    image_size = None
    pose_paths = sorted((dataset / "poses").glob("pose_*"))
    for path in pose_paths:
        image_path = path / "image.jpg"
        if not image_path.is_file():
            continue
        image = cv2.imread(str(image_path), cv2.IMREAD_COLOR)
        if image is None:
            print(f"跳过无法读取图像：{image_path}")
            continue
        h, w = image.shape[:2]
        if image_size is None:
            image_size = (w, h)
        elif image_size != (w, h):
            print(f"跳过分辨率不同图像：{image_path} {w}x{h}")
            continue
        tags, duplicates = detect_tags(image, ar)
        if len(tags) < 3:
            print(f"跳过 {path.name}：仅检测到标记 {sorted(tags)}")
            continue
        obj, img = [], []
        for tid in sorted(tags):
            obj.extend(tag_object_corners(tid))
            img.extend(tags[tid])
        observations.append({"path": path, "image": image, "object": np.asarray(obj, np.float32),
                             "pixels": np.asarray(img, np.float32), "tags": sorted(tags),
                             "duplicates": duplicates})
    if len(observations) < 8:
        raise RuntimeError(f"至少需要 8 个有效姿态，当前只有 {len(observations)}")
    return observations, image_size


def _board_hole_ok(centered: np.ndarray, vt: np.ndarray,
                   u_span: float, v_span: float, r_hole: float = 0.12) -> bool:
    """True if the projected inlier set shows the board's 4-hole signature."""
    if u_span < 0.4 or v_span < 0.4:
        return False
    u = centered @ vt[0]
    v = centered @ vt[1]
    holes = np.asarray([[-0.25, -0.2], [0.25, -0.2], [-0.25, 0.2], [0.25, 0.2]], np.float64)
    area_board = u_span * v_span
    for perm in ((0, 1), (1, 0)):
        su, sv = (u_span, v_span) if perm == (0, 1) else (v_span, u_span)
        hu = holes[:, 0] * (su / 1.4)
        hv = holes[:, 1] * (sv / 1.0)
        in_hole = np.zeros(len(u), dtype=bool)
        for k in range(4):
            in_hole |= (np.abs(u - hu[k]) < r_hole) & (np.abs(v - hv[k]) < r_hole)
        area_holes = 4 * np.pi * r_hole * r_hole
        area_out = max(area_board - area_holes, 1e-9)
        dens_hole = in_hole.sum() / area_holes
        dens_out = (~in_hole).sum() / area_out
        if dens_hole < 0.45 * dens_out:
            return True
    return False


def fit_lidar_board_plane(points: np.ndarray, threshold: float = 0.04,
                          iterations: int = 9000,
                          radius_range: tuple | None = None,
                          seed: int = 20260929):
    points = np.asarray(points, dtype=np.float64).reshape(-1, 3)
    points = points[np.isfinite(points).all(axis=1)]
    radii = np.linalg.norm(points, axis=1)
    mask = (radii > 0.15) & (radii < 30.0)
    if radius_range is not None:
        # Board corners (camera PnP, lidar frame approx) bound the radius band
        # that covers the whole tilted board; walls behind fall outside.
        r_min, r_max = float(radius_range[0]), float(radius_range[1])
        mask &= (radii >= r_min) & (radii <= r_max)
    points = points[mask]
    if len(points) < 30:
        raise RuntimeError(f"点云有效点过少：{len(points)}")
    rng = np.random.default_rng(seed)
    work = points if len(points) <= 6000 else points[rng.choice(len(points), 6000, replace=False)]
    best = None
    for _ in range(iterations):
        idx = rng.choice(len(work), 3, replace=False)
        a, b, c = work[idx]
        normal = np.cross(b-a, c-a)
        norm = np.linalg.norm(normal)
        if norm < 1e-6:
            continue
        normal /= norm
        d = float(normal @ a)
        inliers = np.flatnonzero(np.abs(work @ normal - d) < threshold)
        if len(inliers) < 25:
            continue
        subset = work[inliers]
        centered = subset - subset.mean(axis=0)
        _, _, vt = np.linalg.svd(centered, full_matrices=False)
        projected = centered @ vt[:2].T
        spans = np.percentile(projected, 97.5, axis=0) - np.percentile(projected, 2.5, axis=0)
        spans.sort()
        short, long = float(spans[0]), float(spans[1])
        ratio = long / max(short, 1e-6)
        geometry = (short >= 0.4 and long <= 2.0 and 1.05 <= ratio <= 2.2)
        if geometry:
            u_span = np.percentile(centered @ vt[0], 97.5) - np.percentile(centered @ vt[0], 2.5)
            v_span = np.percentile(centered @ vt[1], 97.5) - np.percentile(centered @ vt[1], 2.5)
            geometry = geometry and _board_hole_ok(centered, vt, float(u_span), float(v_span))
        score = len(inliers) * (1.0 if geometry else 0.12)
        if best is None or score > best[0]:
            best = (score, normal, d, subset, short, long, geometry)
    if best is None or not best[6]:
        raise RuntimeError("没有找到尺寸接近 1400x1000 mm 的板面。检查点云坐标、靶板可见性和采集姿态。")
    _, normal, d, ransac_inliers, _, _, _ = best
    # Refine with the RANSAC inlier subset first (keeps board, not wall/floor),
    # then re-project all points onto the plane with the tight threshold.
    center = ransac_inliers.mean(axis=0)
    _, _, vt = np.linalg.svd(ransac_inliers - center, full_matrices=False)
    normal = vt[-1]
    d = float(normal @ center)
    # Plane normal points from target toward LiDAR origin.
    if normal @ center > 0:
        normal = -normal
        d = -d
    dist = np.abs(points @ normal - d)
    inliers = points[dist < threshold]
    if len(inliers) < 25:
        raise RuntimeError("板面拟合内点不足")
    center = inliers.mean(axis=0)
    _, _, vt = np.linalg.svd(inliers - center, full_matrices=False)
    normal = vt[-1]
    d = float(normal @ center)
    if normal @ center > 0:
        normal = -normal
        d = -d
    signed = points @ normal - d
    inliers = points[np.abs(signed) < threshold]
    center = inliers.mean(axis=0)
    _, _, vt = np.linalg.svd(inliers - center, full_matrices=False)
    proj = (inliers - center) @ vt[:2].T
    spans = np.percentile(proj, 97.5, axis=0) - np.percentile(proj, 2.5, axis=0)
    spans.sort()
    short, long = float(spans[0]), float(spans[1])
    if not (short >= 0.4 and long <= 2.0
            and 1.05 <= long / max(short, 1e-6) <= 2.2):
        print(f"[plane] 复核失败 inliers={len(inliers)} spans=[{short:.3f},{long:.3f}] "
              f"ratio={long/max(short,1e-6):.2f}", file=sys.stderr)
        return None
    residuals = points @ normal - d
    residuals = residuals[np.abs(residuals) < threshold]
    return {"normal": normal / np.linalg.norm(normal), "d": float(d),
            "inliers": int(len(inliers)), "points": int(len(points)),
            "rms_m": float(np.sqrt(np.mean(residuals**2))),
            "spans_m": [float(spans[1]), float(spans[0])],
            "center": center, "_inlier_points": inliers}


def solve_unguided(args) -> None:
    """Historical radius-only solver kept for comparison; CLI uses guided solve."""
    dataset = args.dataset.resolve()
    observations, image_size = collect_image_observations(dataset)
    if len(observations) < args.min_poses:
        raise RuntimeError(f"有效姿态 {len(observations)} 少于要求的 {args.min_poses}。")
    obj = [o["object"] for o in observations]
    img = [o["pixels"] for o in observations]
    rms, K, distortion, rvecs, tvecs = cv2.calibrateCamera(
        obj, img, image_size, None, None)
    distortion = distortion.reshape(-1)
    view_report = []
    camera_normals, camera_ds, lidar_normals, lidar_ds = [], [], [], []
    plane_inliers_by_pose = {}
    for i, observation in enumerate(observations):
        projected, _ = cv2.projectPoints(observation["object"], rvecs[i], tvecs[i], K, distortion)
        err = np.linalg.norm(projected.reshape(-1, 2) - observation["pixels"], axis=1)
        R_board, _ = cv2.Rodrigues(rvecs[i])
        n_cam = R_board[:, 2]
        t_cam = tvecs[i].reshape(3)
        d_cam = float(n_cam @ t_cam)
        item = {"pose": observation["path"].name, "tag_ids": observation["tags"],
                "duplicate_ids": observation["duplicates"],
                "reprojection_rms_px": float(np.sqrt(np.mean(err**2))),
                "reprojection_max_px": float(np.max(err)), "lidar_plane": None}
        cloud_path = observation["path"] / "points.npz"
        if cloud_path.is_file():
            with np.load(cloud_path) as data:
                points = data["points"]
            corners_board = np.asarray([[0.0, 0.0, 0.0], [1.4, 0.0, 0.0],
                                          [1.4, 1.0, 0.0], [0.0, 1.0, 0.0]], np.float64)
            corners_cam = (R_board @ corners_board.T).T + t_cam.reshape(3)
            r_corners = np.linalg.norm(corners_cam, axis=1)
            r_range = (float(r_corners.min()) - 0.35, float(r_corners.max()) + 0.35)
            plane = None
            for seed in (20260929, 17, 77):
                try:
                    pl = fit_lidar_board_plane(points, radius_range=r_range, seed=seed)
                except RuntimeError:
                    pl = None
                if pl is not None and (plane is None
                                       or pl["_inlier_points"].shape[0] > plane["_inlier_points"].shape[0]):
                    plane = pl
            if plane is None:
                print(f"[solve] {observation['path'].name}: 跳过（板面拟合失败）", file=sys.stderr)
                view_report.append(item)
                continue
            n_cam = n_cam / np.linalg.norm(n_cam)
            # PnP's board +Z normal should face the camera; normalize sign defensively.
            if n_cam @ t_cam > 0:
                n_cam, d_cam = -n_cam, -d_cam
            camera_normals.append(n_cam)
            camera_ds.append(d_cam)
            lidar_normals.append(plane["normal"])
            lidar_ds.append(plane["d"])
            plane_inliers_by_pose[observation["path"].name] = plane["_inlier_points"]
            item["lidar_plane"] = {k: (v.tolist() if isinstance(v, np.ndarray) else v)
                                   for k, v in plane.items() if not k.startswith("_")}
        else:
            print(f"缺少点云：{cloud_path}")
        view_report.append(item)

    if len(camera_normals) < args.min_poses:
        raise RuntimeError(f"有效相机/雷达配对只有 {len(camera_normals)} 组，至少需要 {args.min_poses}。")
    A = np.asarray(lidar_normals, dtype=np.float64)
    B = np.asarray(camera_normals, dtype=np.float64)
    # Kabsch with outlier rejection: wall-plane fits poison the normal set, so
    # iterate a few times dropping the worst normal-alignment residuals.
    keep = np.ones(len(A), dtype=bool)
    R_lidar_camera = None
    t_lidar_camera = None
    trans_singular = None
    for _ in range(4):
        u, singular_values, vt = np.linalg.svd(A[keep].T @ B[keep])
        correction = np.eye(3)
        correction[2, 2] = np.linalg.det(vt.T @ u.T)
        Rc = vt.T @ correction @ u.T
        b = np.asarray(camera_ds)[keep] - np.asarray(lidar_ds)[keep]
        tc, _, rank, ts = np.linalg.lstsq(B[keep], b, rcond=None)
        nerr = np.arccos(np.clip(np.sum((A @ Rc.T) * B, axis=1), -1, 1))
        R_lidar_camera, t_lidar_camera, trans_singular = Rc, tc, ts
        if len(nerr) <= args.min_poses or np.max(nerr) < np.radians(12.0):
            break
        order = np.argsort(nerr)[::-1]
        n_drop = int(np.sum(keep)) - args.min_poses
        if n_drop <= 0:
            break
        keep[order[:n_drop]] = False
    normal_errors = np.arccos(np.clip(np.sum((A @ R_lidar_camera.T) * B, axis=1), -1, 1))
    plane_errors = B @ t_lidar_camera - (np.asarray(camera_ds) - np.asarray(lidar_ds))
    det = float(np.linalg.det(R_lidar_camera))
    orthogonality = float(np.linalg.norm(R_lidar_camera.T @ R_lidar_camera - np.eye(3)))
    coverage = np.linalg.svd(B, compute_uv=False)
    dist_list = distortion.tolist()
    k1, k2, p1, p2 = (dist_list + [0, 0, 0, 0])[:4]
    k3 = dist_list[4] if len(dist_list) > 4 else 0.0
    device_sn = args.sn
    camera = {"model": "MC800S", "width": image_size[0], "height": image_size[1],
              "fx": float(K[0, 0]), "fy": float(K[1, 1]),
              "cx": float(K[0, 2]), "cy": float(K[1, 2]),
              "distortion": {"k1": k1, "k2": k2, "p1": p1, "p2": p2, "k3": k3}}
    ext = {"R": R_lidar_camera.tolist(), "t": t_lidar_camera.tolist(), "unit": "m",
           "transform": "camera_point = R * lidar_point + t"}
    output = args.output.resolve() if args.output else (dataset / "result")
    output.mkdir(parents=True, exist_ok=True)
    diagnostics = output / "diagnostics"
    diagnostics.mkdir(exist_ok=True)
    for observation, view in zip(observations, view_report):
        image = observation["image"].copy()
        found, _ = detect_tags(image)
        for tid, corners in found.items():
            cv2.polylines(image, [corners.astype(np.int32)], True, (0, 255, 0), 2)
            cv2.putText(image, str(tid), tuple(corners[0].astype(int)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)
        pose_index = int(observation["path"].name.split("_")[-1])
        if observation["path"].name in plane_inliers_by_pose:
            lidar_points = plane_inliers_by_pose[observation["path"].name]
            points_cam = (R_lidar_camera @ np.asarray(lidar_points).T).T + t_lidar_camera
            visible = points_cam[:, 2] > 0.1
            if np.any(visible):
                projected, _ = cv2.projectPoints(points_cam[visible].astype(np.float64),
                                                 np.zeros(3), np.zeros(3), K, distortion)
                for u, v in projected.reshape(-1, 2)[::max(1, len(projected)//3000)]:
                    if 0 <= u < image.shape[1] and 0 <= v < image.shape[0]:
                        cv2.circle(image, (int(round(u)), int(round(v))), 2, (255, 80, 0), -1)
        label = f"{observation['path'].name}  reproj={view['reprojection_rms_px']:.2f}px"
        cv2.putText(image, label, (18, 32), cv2.FONT_HERSHEY_SIMPLEX,
                    0.75, (0, 0, 255), 2, cv2.LINE_AA)
        cv2.imwrite(str(diagnostics / f"{pose_index:03d}_lidar_overlay.jpg"), image,
                    [cv2.IMWRITE_JPEG_QUALITY, 92])
    camera_json = {"device_sn": device_sn, "resolution": list(image_size),
                   "camera": camera, "rms_reprojection_px": float(rms),
                   "calibration_status": "candidate", "valid": False}
    ext_json = {"device_sn": device_sn, "lidar_to_camera": ext,
                "normal_alignment_rms_deg": float(np.degrees(np.sqrt(np.mean(normal_errors**2)))),
                "plane_offset_rms_m": float(np.sqrt(np.mean(plane_errors**2))),
                "translation_observability_singular_values": trans_singular.tolist(),
                "valid": False}
    calibration = {"calibration_status": "candidate_review_required", "valid": False,
                   "physical_calibration_performed": (dataset / "capture.jsonl").is_file(), "device_sn": device_sn,
                   "version": time.strftime("cal-candidate-%Y%m%d-%H%M%S"),
                   "camera": camera, "lidar_to_camera": {"R": R_lidar_camera.tolist(),
                   "t": t_lidar_camera.tolist(), "unit": "m"},
                   "method": "local ArUco board poses + multi-pose LiDAR plane constraints",
                   "note": "Candidate only. Review data, residuals, axes, and independent overlays before approval/deployment."}
    (output / "camera_intrinsics.json").write_text(json.dumps(camera_json, indent=2), encoding="utf-8")
    (output / "lidar_camera_extrinsics.json").write_text(json.dumps(ext_json, indent=2), encoding="utf-8")
    (output / "calibration_candidate.json").write_text(json.dumps(calibration, indent=2), encoding="utf-8")
    summary = {"dataset": str(dataset), "opencv": cv2.__version__, "dictionary": DICT_NAME,
               "marker_ids": [1, 2, 3, 4], "board_m": {"width": BOARD_W, "height": BOARD_H,
               "tag_size": TAG_SIZE, "tag_origins": TAG_ORIGINS,
               "hole_centers": HOLE_CENTERS, "hole_diameter": 0.240},
               "valid_views": len(observations), "image_size": list(image_size),
               "intrinsic_global_rms_px": float(rms), "camera_matrix": K.tolist(),
               "distortion_coefficients": dist_list, "extrinsic_valid_pose_pairs": len(camera_normals),
               "rotation_det": det, "rotation_orthogonality_error": orthogonality,
               "translation_rank": int(rank), "plane_normal_singular_values": coverage.tolist(),
               "normal_alignment_rms_deg": float(np.degrees(np.sqrt(np.mean(normal_errors**2)))),
               "plane_offset_rms_m": float(np.sqrt(np.mean(plane_errors**2))),
               "poses": view_report,
               "status": "CANDIDATE_REVIEW_REQUIRED",
               "warning": "Do not deploy automatically. Board tags are sparse; inspect per-pose residuals, axes, and held-out overlays."}
    (output / "report.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({k: summary[k] for k in ("status", "valid_views", "intrinsic_global_rms_px",
        "extrinsic_valid_pose_pairs", "rotation_det", "rotation_orthogonality_error",
        "translation_rank", "normal_alignment_rms_deg", "plane_offset_rms_m")}, indent=2))
    print(f"候选结果已写入本机：{output}")


def solve(args) -> None:
    from tools.calibration.guided_solver import solve as guided_solve
    guided_solve(args, collect_image_observations)


def main():
    parser = argparse.ArgumentParser(description="本地 MC800S + XT-M60 标定采集与离线求解")
    subs = parser.add_subparsers(dest="command", required=True)
    cap = subs.add_parser("capture", help="单 Zenoh 会话采集 JPEG/H.264 + 点云姿态")
    cap.add_argument("--endpoint", default="tcp/198.51.100.179:7447#bind=198.51.100.200:0")
    cap.add_argument("--sn", default="DEVICE00000001")
    cap.add_argument("--output", type=Path, required=True)
    cap.add_argument("--reference-camera", type=Path,
                     help="Previously solved camera_intrinsics.json for live board pitch/yaw guidance")
    cap.add_argument("--max-pair-delta-ms", type=float, default=150.0,
                     help="最大允许的相机/雷达消息时间戳差；超出则拒绝采集")
    cap.set_defaults(func=capture)
    sol = subs.add_parser("solve", help="从本机采集数据求相机内参与 LiDAR->相机外参")
    sol.add_argument("--dataset", type=Path, required=True)
    sol.add_argument("--output", type=Path)
    sol.add_argument("--sn", default="DEVICE00000001")
    sol.add_argument("--min-poses", type=int, default=12)
    sol.add_argument("--max-pair-delta-ms", type=float, default=150.0,
                     help="求解允许的最大相机/雷达时间戳差")
    sol.add_argument("--baseline-m", type=float, required=True,
                     help="相机光心与雷达测量原点的实测距离（米）；不可使用外壳间隙代替")
    sol.add_argument("--max-baseline-m", type=float, required=True,
                     help="基于机械尺寸确定的最大物理基线（米）")
    sol.add_argument("--legacy-mirrored-y", action="store_true",
                     help="仅对修正前采集的旧点云在内存中翻转 Y 轴；原始文件不变")
    sol.set_defaults(func=solve)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
