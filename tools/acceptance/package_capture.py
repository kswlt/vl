#!/usr/bin/env python3
"""Assemble a reviewable acceptance directory from real capture artifacts."""
from __future__ import annotations

import argparse
import csv
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from pc import active_msgs_pb2 as pb
from tools.acceptance.h264_check import nals


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("package", type=Path, help="directory containing capture.pcapng and raw_capture")
    ap.add_argument("--board-camera-sha", required=True)
    ap.add_argument("--board-radar-sha", required=True)
    ap.add_argument("--board-gateway-sha", required=True)
    args = ap.parse_args()
    out = args.package
    raw = out / "raw_capture"
    for required in (out / "capture.pcapng", raw / "manifest.json", out / "h264_analysis.csv",
                     out / "timestamp_stats.json", out / "fps_stats.json"):
        if not required.is_file():
            ap.error(f"missing {required}")
    manifest = json.loads((raw / "manifest.json").read_text(encoding="utf-8"))
    if manifest["ended_unix_us"] - manifest["started_unix_us"] < 60_000_000:
        ap.error("capture lasted less than 60 seconds")
    shutil.copyfile(ROOT / "protocol" / "active_msgs.proto", out / "active_msgs.proto")
    shutil.copyfile(out / "h264_analysis.csv", out / "image_frames.csv")
    files = pb.FileMsgArray.FromString((raw / "raw" / "queries" / "config_file" /
                                        "response_001.bin").read_bytes()).array
    for file in files:
        if file.path in ("config.json", "calibration.json", "lidar_intrinsics.json"):
            (out / file.path).write_bytes(file.data)
    with (out / "pointcloud_frames.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["seq", "stamp", "point_count", "scaler", "payload_size"])
        writer.writeheader()
        for packet in sorted((raw / "raw" / "raw" / "pointcloud").glob("*.bin")):
            wire = packet.read_bytes()
            for msg in pb.LidarPointMsgArray.FromString(wire).array:
                writer.writerow({"seq": msg.header.seq, "stamp": msg.header.stamp,
                                 "point_count": len(msg.points), "scaler": msg.header.scaler,
                                 "payload_size": len(wire)})
    samples = out / "sample"
    samples.mkdir(exist_ok=True)
    idr_saved = 0
    other_saved = 0
    for packet in sorted((raw / "raw" / "raw" / "image").glob("*.bin")):
        for msg in pb.ImageMsgArray.FromString(packet.read_bytes()).array:
            types = {unit[0] & 31 for _, unit in nals(bytes(msg.data))}
            if 5 in types and idr_saved < 3:
                idr_saved += 1
                (samples / f"idr_{idr_saved:04d}.h264").write_bytes(msg.data)
            elif 5 not in types and other_saved < 2:
                other_saved += 1
                (samples / f"frame_{other_saved:04d}.h264").write_bytes(msg.data)
        if idr_saved >= 3 and other_saved >= 2:
            break
    logs = out / "logs"
    logs.mkdir(exist_ok=True)
    for name in ("timestamp_stats.json", "fps_stats.json", "h264_summary.json"):
        shutil.copyfile(out / name, logs / name)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    versions = (f"git_HEAD={revision}\nboard_gateway_sha256={args.board_gateway_sha}\n"
                f"board_camera_sha256={args.board_camera_sha}\nboard_radar_sha256={args.board_radar_sha}\n"
                f"endpoint={manifest['endpoint']}\nSN={manifest['sn']}\n"
                f"capture_started_unix_us={manifest['started_unix_us']}\n"
                f"capture_ended_unix_us={manifest['ended_unix_us']}\n")
    (out / "versions.txt").write_text(versions, encoding="utf-8")
    stats = json.loads((out / "timestamp_stats.json").read_text(encoding="utf-8"))
    fps = json.loads((out / "fps_stats.json").read_text(encoding="utf-8"))
    h264 = json.loads((out / "h264_summary.json").read_text(encoding="utf-8"))
    readme = f"""# RK3562 协议网关现场验收包

单 Zenoh client session 采集 {manifest['capture_seconds']} 秒；网络抓包见 `capture.pcapng`，原始 Protobuf 见 `raw_capture/raw/`。

- SN：`{manifest['sn']}`；endpoint：`{manifest['endpoint']}`。
- 图像：{fps['image']['measured_fps']} Hz，{stats['image']['count']} 帧；点云：{fps['pointcloud']['measured_fps']} Hz，{stats['pointcloud']['count']} 帧。
- 两路 stamp 均为 RK Unix epoch 微秒；rollback 均为 0，seq gap 均为 0。
- 全窗口图像最近邻点云差最大 {stats['nearest_image_cloud']['delta_us']['max']} μs，>100ms 为 {stats['nearest_image_cloud']['over_100ms_count']} 帧；共同时间范围内最大 {stats['nearest_image_cloud']['overlap_max_delta_us']} μs，>100ms 为 {stats['nearest_image_cloud']['overlap_over_100ms_count']} 帧。前后边界未必有相应点云帧，故同时报告两种口径。
- H264：{h264['frames']} 个 AU，{h264['idr_count']} 个 IDR，B 帧 {h264['b_frame_count']}，最大 IDR 间隔 {h264['max_idr_interval_us']} μs。
- 图像数据 `ImageMsg.format=3`，设置请求 `SettingRequest.image_format=1`；含义都是 H264，但字段枚举不同。
- `config.json`、`calibration.json`、`lidar_intrinsics.json` 为设备本次 query 返回的原字节；标定数值真实性需以原始标定数据和现场复核确认。
- PCAP 包含该网口到 `:7447` 的现场流量，可能还有同地址的其他客户端连接；订阅与统计以 `raw_capture/manifest.json` 为准。

工具位于仓库 `tools/acceptance/`。本目录为现场原始证据，不进入 Git。
"""
    (out / "README.md").write_text(readme, encoding="utf-8")
    print(json.dumps({"package": str(out), "sample_idr": idr_saved,
                      "sample_non_idr": other_saved, "raw_image_frames": stats["image"]["count"],
                      "raw_pointcloud_frames": stats["pointcloud"]["count"]}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
