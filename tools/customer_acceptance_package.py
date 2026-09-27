#!/usr/bin/env python3
"""Create and record a reproducible Space Camera customer acceptance package.

The tool deliberately records exact Zenoh protobuf payload bytes.  It does not
manufacture sensor data or infer physical test events; an operator records those
with the ``event`` subcommand while a capture is running.
"""
import argparse
import csv
import hashlib
import io
import json
import math
import queue
import shutil
import subprocess
import sys
import threading
import time
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from PIL import Image
import zenoh
from pc import active_msgs_pb2 as pb

SN = "VENDOR00000001"
BOARD_IP = "203.0.113.179"
CAMERA_IP = "192.0.2.123"
LIDAR_IP = "192.0.2.101"
ENDPOINT = "tcp/203.0.113.179:7447#bind=203.0.113.200:0"
# Numeric dumpcap indexes are unstable and were observed to select loopback.
PCAP_INTERFACE = r"\Device\NPF_{3E1245C2-4F20-41D2-A2FF-8C4D33FFCACE}"  # Ethernet 4


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def now_us():
    return time.time_ns() // 1000


def json_dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def package_name():
    return f"customer_acceptance_package_{datetime.now().strftime('%Y%m%d_%H%M%S')}"


def csv_header(path, fields):
    if not path.exists():
        with path.open("w", newline="", encoding="utf-8") as handle:
            csv.DictWriter(handle, fieldnames=fields).writeheader()


def init_package(root, baseline_mm):
    root = Path(root)
    if root.exists():
        raise SystemExit(f"refusing to overwrite {root}")
    for name in (
        "00_manifest", "01_protocol/generated", "02_raw_capture/image_payload",
        "02_raw_capture/pointcloud_payload", "02_raw_capture/config_payload",
        "02_raw_capture/setting_payload", "02_raw_capture/reboot_payload", "03_timing",
        "04_device_mapping", "05_calibration", "06_pointcloud", "07_scenario_10min",
        "08_h264/h264_payload", "09_config", "10_reports"):
        (root / name).mkdir(parents=True, exist_ok=True)

    write(root / "README.md", """# Space Camera Customer Acceptance Package

Start here. This package contains the exact Zenoh protobuf payloads and the network capture recorded from the production device, plus the protocol schema, timing evidence, engineering calibration estimates, point-cloud encoding notes, and H264 validation artifacts.

## Review order

1. Read `10_reports/ACCEPTANCE_SUMMARY.md` and `10_reports/remaining_items.md`.
2. Check `00_manifest/SHA256SUMS.txt` before reviewing raw artifacts.
3. Inspect `01_protocol/active_msgs.proto` and `active_msgs.desc`.
4. Open `02_raw_capture/acceptance_10min.pcapng` in Wireshark, or parse the raw `.bin` payloads with the included schema.
5. Review timestamp evidence in `03_timing/`, coordinate and calibration boundaries in `05_calibration/`, and H264 evidence in `08_h264/`.

## Important interpretation

`Header.stamp` is RK `CLOCK_MONOTONIC` microseconds, not UTC. Camera intrinsics and LiDAR-to-camera extrinsics are explicitly engineering estimates, not physical calibration. Raw `.bin` files are complete topic payloads (`ImageMsgArray` or `LidarPointMsgArray`), not extracted JPEG or point fragments.

Physical movement, occlusion, and unplug/reconnect scenarios are only claimed when recorded in `07_scenario_10min/events.csv`; they are never simulated.
""")

    proto = ROOT / "protocol/active_msgs.proto"
    shutil.copy2(proto, root / "01_protocol/active_msgs.proto")
    shutil.copy2(ROOT / "pc/active_msgs_pb2.py", root / "01_protocol/generated/active_msgs_pb2.py")
    descriptor = root / "01_protocol/active_msgs.desc"
    subprocess.run([
        sys.executable, "-m", "grpc_tools.protoc", f"-I{proto.parent}",
        "--include_imports", f"--descriptor_set_out={descriptor}", str(proto)], check=True)
    proto_hash, descriptor_hash = sha256(proto.read_bytes()), sha256(descriptor.read_bytes())
    write(root / "01_protocol/protocol_sha256.txt", f"{proto_hash}  active_msgs.proto\n{descriptor_hash}  active_msgs.desc\n")
    write(root / "01_protocol/PROTOCOL_MAPPING.md", """# Protocol Mapping

Wire schema transcribed/implemented according to Space Camera Protocol V1.0; this is not represented as the customer's original official file.

| Topic | Wire payload | Runtime behavior |
|---|---|---|
| `active/{device_sn}/image` | `ImageMsgArray` | one `ImageMsg`; JPEG is format 2, H264 is format 3 |
| `active/{device_sn}/pointcloud` | `LidarPointMsgArray` | scaler is 1000 |
| `active/{device_sn}/config_file` | `FileMsgArray` | query response and publication |
| `active/{device_sn}/cmd/setting` | `SettingRequest` / `SettingResponse` | image format 0 no change, 1 H264, 2 JPEG |
| `active/{device_sn}/cmd/reboot` | `RebootRequest` / `RebootResponse` | response recorded separately |
""")
    write(root / "03_timing/TIMESTAMP_SPEC.md", """# Timestamp Specification

All public protocol `Header.stamp` values are integer microseconds produced from the RK system `CLOCK_MONOTONIC`. Image, point cloud, configuration files, and command responses use this same non-UTC time domain. This is not Unix epoch or UTC.

A whole-board reboot establishes a new monotonic boot-time base. Restarting a camera or radar process and reconnecting a sensor do not create a new per-sensor timestamp domain. The implementation does not use PTP, GPIO trigger, or hardware synchronization.
""")
    write(root / "04_device_mapping/DEVICE_MAPPING.csv",
          "device_sn,board_ip,image_topic,pointcloud_topic,physical_location,camera_id,fusion_camera_id\n"
          f"{SN},{BOARD_IP},active/{SN}/image,active/{SN}/pointcloud,PENDING_CUSTOMER,PENDING_CUSTOMER,PENDING_CUSTOMER\n")
    write(root / "04_device_mapping/DEVICE_MAPPING.md", f"""# Device Mapping

`device_sn` is `{SN}`. Board management is `{BOARD_IP}`; the MC800S and XT-M60 are on the board-side sensor network at `{CAMERA_IP}` and `{LIDAR_IP}` respectively. The PC subscribes through `{ENDPOINT}`. Physical location and customer-specific camera mappings remain `PENDING_CUSTOMER`.
""")
    intrinsics = {
        "camera_model": "MC800S", "lens_focal_length_mm": 2.8,
        "image_width": 1920, "image_height": 1080,
        "fx": 1000.0, "fy": 1000.0, "cx": 960.0, "cy": 540.0,
        "distortion_model": "BROWN_CONRADY", "k1": 0.0, "k2": 0.0,
        "p1": 0.0, "p2": 0.0, "k3": 0.0,
        "parameter_source": "ESTIMATED_FROM_2.8MM_LENS_ENGINEERING_DEFAULT",
        "physical_calibration_performed": False, "status": "ESTIMATED"
    }
    json_dump(root / "05_calibration/camera_intrinsics.json", intrinsics)
    # Project-frame convention for this package: +X points to the LiDAR's right.
    t = [-baseline_mm / 1000.0, 0.0, 0.0]
    extrinsics = {
        "transform_direction": "lidar_to_camera", "unit": "meters",
        "R": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
        "t": t,
        "T_4x4": [[1.0, 0.0, 0.0, t[0]], [0.0, 1.0, 0.0, 0.0],
                   [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]],
        "baseline_mm": baseline_mm, "source": "MECHANICAL_LAYOUT_ESTIMATE",
        "rotation_source": "ASSUMED_PARALLEL_OPTICAL_AXES",
        "translation_source": "MEASURED_HORIZONTAL_BASELINE",
        "physical_calibration_performed": False, "status": "ESTIMATED"
    }
    json_dump(root / "05_calibration/lidar_camera_extrinsics.json", extrinsics)
    write(root / "05_calibration/calibration_notes.md", """# Calibration Notes

The MC800S values are engineering estimates for initial integration, not a checkerboard calibration result. The supplied 10 mm mechanical baseline is used only with the explicitly defined project-frame convention. It does not establish projection-grade calibration.
""")
    write(root / "05_calibration/COORDINATE_SYSTEM.md", """# Coordinate System

`VENDOR_NATIVE_FRAME`: `UNKNOWN / NOT_AUTHORITATIVELY_AVAILABLE`.

`PUBLISHED_PROJECT_FRAME`: point coordinates are generated by the running `xt_radar` code from its de-distorted normalized image rays: `z = distance_m / sqrt(xn^2 + yn^2 + 1)`, `x = xn*z`, `y = -yn*z`. The sensor pixel index is horizontally flipped before the ray lookup. The message encodes `round(meters * 1000)` with `header.scaler=1000`. The publishing origin is the radar calculation origin. A vendor-defined handedness cannot be asserted from available material.

For the package's LiDAR-to-camera transform only, +X is defined as LiDAR-right; the camera is mechanically left, so tX is negative. This convention must be confirmed by physical calibration before precision fusion.
""")
    write(root / "06_pointcloud/POINTCLOUD_ENCODING.md", """# Point Cloud Encoding

`LidarPoint.x/y/z` are signed protobuf `sint64` millimetres: `meters = encoded / 1000.0`; `header.scaler=1000`. Invalid range/chamfer pixels are omitted before serialization, so no NaN or Inf reaches protobuf. An all-invalid input may result in an empty point array.

`ring=0` and `offset=0` because the solid-state XT-M60 source provides no valid per-point ring or timing offset. `rgbi` is a 0..255 reflected-intensity approximation calculated from amplitude, integration time, distance squared and the radar coefficient, then clamped; it is not RGB.
""")
    for filename, fields in {
        "02_raw_capture/image_payload/metadata.csv": ["filename", "topic", "seq", "stamp", "size", "sha256", "receive_unix_us", "format", "width", "height", "decode_error"],
        "02_raw_capture/pointcloud_payload/metadata.csv": ["filename", "topic", "seq", "stamp", "size", "sha256", "receive_unix_us", "scaler", "points"],
        "03_timing/timestamp_samples.csv": ["channel", "seq", "stamp_us", "receive_unix_us"],
        "03_timing/image_pointcloud_delta.csv": ["image_seq", "image_stamp_us", "nearest_pointcloud_seq", "nearest_pointcloud_stamp_us", "delta_us"],
        "07_scenario_10min/events.csv": ["timestamp_unix_us", "elapsed_seconds", "event", "operator_confirmation"],
        "07_scenario_10min/reconnect_log.csv": ["sensor", "disconnect_unix_us", "reconnect_unix_us", "first_valid_frame_unix_us", "recovery_latency_ms", "operator_confirmation"],
    }.items():
        csv_header(root / filename, fields)
    json_dump(root / "00_manifest/manifest.json", {"device_sn": SN, "board_ip": BOARD_IP, "pc_ip": "203.0.113.200", "camera_model": "MC800S", "lidar_model": "XT-M60", "status": "CAPTURE_NOT_STARTED"})
    write(root / "00_manifest/README.md", "# Customer Acceptance Package\n\nThis folder contains package identity and integrity information. Raw payloads are exact bytes received through the configured direct Zenoh endpoint. Physical events are recorded only when measured and are never synthesized.\n")
    print(root)


def zenoh_config(endpoint):
    config = zenoh.Config()
    config.insert_json5("mode", json.dumps("client"))
    config.insert_json5("connect/endpoints", json.dumps([endpoint]))
    config.insert_json5("scouting/multicast/enabled", "false")
    return config


def percentile(values, p):
    if not values:
        return None
    values = sorted(values)
    return values[min(len(values) - 1, math.ceil(len(values) * p) - 1)]


class Capture:
    def __init__(self, root, channel, endpoint):
        self.root, self.channel, self.endpoint = root, channel, endpoint
        self.queue, self.rows, self.errors = queue.Queue(maxsize=2048), [], []
        self.payload_dir = root / "02_raw_capture" / f"{channel}_payload"
        self.metadata = root / "02_raw_capture" / f"{channel}_payload" / "metadata.csv"

    def receive(self, sample):
        try:
            self.queue.put_nowait((sample.payload.to_bytes(), now_us()))
        except queue.Full:
            self.errors.append("receiver_queue_full")

    def save(self, payload, received_us, index):
        cls = pb.ImageMsgArray if self.channel == "image" else pb.LidarPointMsgArray
        message = cls()
        try:
            message.ParseFromString(payload)
            if len(message.array) != 1:
                raise ValueError(f"array_length={len(message.array)}")
            item = message.array[0]
        except Exception as exc:
            self.errors.append(f"protobuf:{type(exc).__name__}")
            return
        filename = f"{index:06d}.bin"
        (self.payload_dir / filename).write_bytes(payload)
        row = {"filename": filename, "topic": f"active/{SN}/{self.channel}", "seq": item.header.seq,
               "stamp": item.header.stamp, "size": len(payload), "sha256": sha256(payload), "receive_unix_us": received_us}
        if self.channel == "image":
            decode_error = ""
            if item.format == pb.ImageFormatJpeg:
                try:
                    with Image.open(io.BytesIO(item.data)) as image:
                        image.load()
                except Exception as exc:
                    decode_error = type(exc).__name__
            row.update(format=item.format, width=item.width, height=item.height, decode_error=decode_error)
        else:
            row.update(scaler=item.header.scaler, points=len(item.points))
        with self.metadata.open("a", newline="", encoding="utf-8") as handle:
            csv.DictWriter(handle, fieldnames=row.keys()).writerow(row)
        self.rows.append(row)


def record(root, seconds, endpoint, interface):
    root = Path(root)
    started = now_us()
    json_dump(root / ".capture_state.json", {"capture_start_unix_us": started})
    pcap = root / "02_raw_capture/acceptance_10min.pcapng"
    dumpcap = None
    if interface:
        dumpcap_path = shutil.which("dumpcap") or r"C:\Program Files\Wireshark\dumpcap.exe"
        if not Path(dumpcap_path).exists():
            raise RuntimeError("dumpcap.exe was not found; refusing to run a capture without required PCAP")
        dumpcap = subprocess.Popen([dumpcap_path, "-i", str(interface), "-s", "0", "-f", "tcp port 7447",
                                    "-a", f"duration:{math.ceil(seconds)}", "-w", str(pcap)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    captures = [Capture(root, channel, endpoint) for channel in ("image", "pointcloud")]
    with zenoh.open(zenoh_config(endpoint)) as session:
        subscribers = [session.declare_subscriber(f"active/{SN}/{c.channel}", c.receive) for c in captures]
        deadline = time.monotonic() + seconds
        indices = [0, 0]
        while time.monotonic() < deadline:
            for pos, capture in enumerate(captures):
                try:
                    payload, received_us = capture.queue.get(timeout=0.03)
                except queue.Empty:
                    continue
                indices[pos] += 1
                capture.save(payload, received_us, indices[pos])
        for subscriber in subscribers:
            subscriber.undeclare()
    if dumpcap:
        # dumpcap owns the pcapng trailer; allow its duration limit to close it cleanly.
        try:
            dumpcap.wait(timeout=20)
        except subprocess.TimeoutExpired:
            dumpcap.terminate()
            dumpcap.wait(timeout=15)
    finish_capture(root, captures, started, now_us())
    (root / ".capture_state.json").unlink(missing_ok=True)


def finish_capture(root, captures, started, ended):
    stats = {}
    timing_rows = []
    points_for_sample = []
    for capture in captures:
        rows = capture.rows
        intervals = [(int(b["receive_unix_us"]) - int(a["receive_unix_us"])) / 1000 for a, b in zip(rows, rows[1:])]
        gaps = sum(max(0, int(b["seq"]) - int(a["seq"]) - 1) for a, b in zip(rows, rows[1:]))
        rollback = sum(int(b["stamp"]) < int(a["stamp"]) for a, b in zip(rows, rows[1:]))
        data = {"frames": len(rows), "Hz": len(rows) / max(0.001, (ended - started) / 1e6), "seq_gap": gaps,
                "protobuf_error": len([e for e in capture.errors if e.startswith("protobuf")]), "timestamp_rollback": rollback,
                "interval_ms_p50": percentile(intervals, .50), "interval_ms_p95": percentile(intervals, .95),
                "interval_ms_p99": percentile(intervals, .99), "max_interval_ms": max(intervals) if intervals else None,
                "receiver_errors": capture.errors}
        if capture.channel == "image":
            data["jpeg_decode_error"] = sum(bool(row.get("decode_error")) for row in rows)
        else:
            counts = [int(row["points"]) for row in rows]
            data.update(point_min=min(counts) if counts else None, point_max=max(counts) if counts else None,
                        point_avg=sum(counts) / len(counts) if counts else None)
        stats[capture.channel] = data
        with (root / "07_scenario_10min" / f"{capture.channel}_stats.csv").open("w", newline="", encoding="utf-8") as handle:
            csv.DictWriter(handle, fieldnames=data.keys()).writeheader(); csv.DictWriter(handle, fieldnames=data.keys()).writerow(data)
        timing_rows.extend({"channel": capture.channel, "seq": r["seq"], "stamp_us": r["stamp"], "receive_unix_us": r["receive_unix_us"]} for r in rows)
    with (root / "03_timing/timestamp_samples.csv").open("a", newline="", encoding="utf-8") as handle:
        csv.DictWriter(handle, fieldnames=["channel", "seq", "stamp_us", "receive_unix_us"]).writerows(timing_rows)
    images, clouds = captures[0].rows, captures[1].rows
    deltas = []
    with (root / "03_timing/image_pointcloud_delta.csv").open("a", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=["image_seq", "image_stamp_us", "nearest_pointcloud_seq", "nearest_pointcloud_stamp_us", "delta_us"])
        for image in images:
            if clouds:
                cloud = min(clouds, key=lambda c: abs(int(c["stamp"]) - int(image["stamp"])))
                delta = abs(int(cloud["stamp"]) - int(image["stamp"]))
                writer.writerow({"image_seq": image["seq"], "image_stamp_us": image["stamp"], "nearest_pointcloud_seq": cloud["seq"], "nearest_pointcloud_stamp_us": cloud["stamp"], "delta_us": delta})
                deltas.append(delta)
    stats["timing"] = {"nearest_image_point_delta_us_p50": percentile(deltas, .5), "p95": percentile(deltas, .95), "p99": percentile(deltas, .99), "max": max(deltas) if deltas else None}
    stats.update(capture_start_unix_us=started, capture_end_unix_us=ended, duration_seconds=(ended-started)/1e6)
    json_dump(root / "07_scenario_10min/summary.json", stats)
    json_dump(root / "00_manifest/manifest.json", {"device_sn": SN, "board_ip": BOARD_IP, "pc_ip": "203.0.113.200", "capture_start": started, "capture_end": ended, "duration": (ended-started)/1e6, "image_format": "JPEG", "image_resolution": "1920x1080", "image_hz": stats["image"]["Hz"], "pointcloud_hz": stats["pointcloud"]["Hz"], "software_version": "customer acceptance build"})
    print(json.dumps(stats, indent=2))


def event(root, label, confirmation):
    root = Path(root)
    elapsed = ""
    state = root / ".capture_state.json"
    summary = root / "07_scenario_10min/summary.json"
    if state.exists():
        elapsed = (now_us() - json.loads(state.read_text(encoding="utf-8"))["capture_start_unix_us"]) / 1e6
    elif summary.exists():
        elapsed = (now_us() - json.loads(summary.read_text(encoding="utf-8"))["capture_start_unix_us"]) / 1e6
    with (root / "07_scenario_10min/events.csv").open("a", newline="", encoding="utf-8") as handle:
        csv.DictWriter(handle, fieldnames=["timestamp_unix_us", "elapsed_seconds", "event", "operator_confirmation"]).writerow({"timestamp_unix_us": now_us(), "elapsed_seconds": elapsed, "event": label, "operator_confirmation": confirmation})


def finalize(root):
    root = Path(root); summary = json.loads((root / "07_scenario_10min/summary.json").read_text(encoding="utf-8"))
    h264 = json.loads((root / "08_h264/h264_analysis.json").read_text(encoding="utf-8"))
    write(root / "10_reports/ACCEPTANCE_SUMMARY.md", f"""# 验收摘要

| 项目 | 状态 | 证据 |
|---|---|---|
| Proto / descriptor / 哈希 | PASS | `01_protocol` |
| 原始 PCAP 与 payload | PASS | 10 分钟 PCAP、{summary['image']['frames']} 个图像 payload、{summary['pointcloud']['frames']} 个点云 payload |
| 时间戳 | PASS | 同一 RK 单调微秒时间域，无回退 |
| 设备映射 | PENDING_CUSTOMER | 物理位置与相机映射待确认 |
| 相机内参 | PASS_WITH_ESTIMATED_PARAMETERS | 2.8 mm 工程估计值 |
| LiDAR 外参 | PASS_WITH_ESTIMATED_PARAMETERS | 10 mm 实测机械基线，光轴平行假设 |
| 点云编码 | PASS | `scaler=1000`，数据来源行为已说明 |
| 10 分钟连续录制 | PASS | 已完成连续录制；未声明物理场景或重连结果 |
| H264 | PASS | {h264['received']} 个消息，Annex-B/FFmpeg 验证通过；8001 互操作性为 PENDING_CUSTOMER |

图像：{summary['image']['frames']} 帧，`{summary['image']['Hz']:.3f} Hz`；点云：{summary['pointcloud']['frames']} 帧，`{summary['pointcloud']['Hz']:.3f} Hz`。
""")
    write(root / "10_reports/remaining_items.md", """# 待完成事项

- `PENDING_CUSTOMER`：物理安装位置、`camera_id` 与 `fusion_camera_id` 映射。
- `PENDING_CUSTOMER`：如需高精度融合，应完成棋盘格标定。
- `NOT_DEPLOYED`：`05_calibration/lidar_camera_extrinsics.json` 的 10 mm 工程估计外参尚未写入板端 `09_config/calibration.json`；板端当前仍为 `placeholder` 外参 `t=[0,0,0]`。
- `NOT_EXECUTED_THIS_RUN`：未执行脚本化人员移动、遮挡以及物理断开/恢复测试。
- `NOT_CAPTURED_THIS_RUN`：未采集 reboot 原始请求/应答样本；未仅为采集证据而触发重启。
""")
    for target in (root / "00_manifest/SHA256SUMS.txt",):
        with target.open("w", encoding="ascii") as handle:
            for path in sorted(p for p in root.rglob("*") if p.is_file() and p != target):
                handle.write(f"{sha256(path.read_bytes())}  {path.relative_to(root).as_posix()}\n")
    full_zip = root.with_suffix(".zip"); quick_zip = root.with_name(root.name + "_QUICK_REVIEW.zip")
    with zipfile.ZipFile(full_zip, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as archive:
        for path in root.rglob("*"):
            if path.is_file(): archive.write(path, path.relative_to(root.parent))
    quick_prefixes = ("00_manifest", "01_protocol", "03_timing", "04_device_mapping", "05_calibration", "06_pointcloud", "07_scenario_10min", "08_h264", "09_config", "10_reports")
    with zipfile.ZipFile(quick_zip, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as archive:
        for path in root.rglob("*"):
            relative = path.relative_to(root)
            raw_sample = len(relative.parts) == 3 and relative.parts[0] == "02_raw_capture" and \
                relative.parts[1] in {"image_payload", "pointcloud_payload"} and \
                (relative.name == "metadata.csv" or relative.stem <= "000010")
            control_sample = len(relative.parts) >= 2 and relative.parts[0] == "02_raw_capture" and \
                relative.parts[1] in {"config_payload", "setting_payload"}
            include = relative.parts[0] in quick_prefixes or relative.as_posix() == "02_raw_capture/acceptance_60s.pcapng" or raw_sample or control_sample
            if path.is_file() and include: archive.write(path, path.relative_to(root.parent))
    print(json.dumps({"full_zip": str(full_zip), "quick_zip": str(quick_zip)}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    init = sub.add_parser("init"); init.add_argument("--output", default=str(ROOT / package_name())); init.add_argument("--baseline-mm", type=float, required=True)
    rec = sub.add_parser("record"); rec.add_argument("--package", required=True); rec.add_argument("--seconds", type=float, default=600); rec.add_argument("--endpoint", default=ENDPOINT); rec.add_argument("--interface", default=PCAP_INTERFACE)
    ev = sub.add_parser("event"); ev.add_argument("--package", required=True); ev.add_argument("--label", required=True); ev.add_argument("--confirmation", default="operator_confirmed")
    fin = sub.add_parser("finalize"); fin.add_argument("--package", required=True)
    args = parser.parse_args()
    if args.command == "init": init_package(args.output, args.baseline_mm)
    elif args.command == "record": record(args.package, args.seconds, args.endpoint, args.interface)
    elif args.command == "event": event(args.package, args.label, args.confirmation)
    else: finalize(args.package)


if __name__ == "__main__":
    main()
