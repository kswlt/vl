#!/usr/bin/env python3
"""Build a fresh checker input from a live RK capture and the static evidence template."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from pc import active_msgs_pb2 as pb


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def iso_utc(unix_us: int) -> str:
    return datetime.fromtimestamp(unix_us / 1_000_000, timezone.utc).isoformat().replace("+00:00", "Z")


def capture_dir(base: Path) -> Path:
    nested = base / "capture"
    return nested if (nested / "manifest.json").is_file() else base


def copy_static(template: Path, output: Path) -> None:
    excluded_dirs = {"image_payload", "pointcloud_payload"}
    excluded_files = {"manifest.json", "SHA256SUMS.txt", "capture_610s.pcap", "capture_70s.pcap",
                      "config_file_request.bin", "config_file_response.bin",
                      "setting_request.bin", "setting_response.bin"}
    for source in template.rglob("*"):
        rel = source.relative_to(template)
        if rel.parts[0] in excluded_dirs or rel.as_posix() in excluded_files:
            continue
        target = output / rel
        if source.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)


def contains_idr(data: bytes) -> bool:
    for index in range(len(data) - 4):
        if data[index:index + 3] == b"\x00\x00\x01":
            return (data[index + 3] & 0x1F) == 5 or contains_idr(data[index + 4:])
        if data[index:index + 4] == b"\x00\x00\x00\x01":
            return (data[index + 4] & 0x1F) == 5 or contains_idr(data[index + 5:])
    return False


def split_payloads(raw: Path, output: Path, capture_manifest: dict) -> None:
    received = {(entry["topic"].rsplit("/", 1)[-1], int(entry["index"])):
                int(entry["received_unix_us"]) for entry in capture_manifest["records"]}

    image_dir = output / "image_payload"
    cloud_dir = output / "pointcloud_payload"
    image_dir.mkdir(parents=True)
    cloud_dir.mkdir(parents=True)

    image_rows: list[dict] = []
    image_messages = []
    for packet_path in sorted((raw / "raw/raw/image").glob("*.bin")):
        packet_index = int(packet_path.stem)
        packet = pb.ImageMsgArray.FromString(packet_path.read_bytes())
        for message in packet.array:
            image_messages.append((message, received[("image", packet_index)]))
    first_idr = next((i for i, (message, _) in enumerate(image_messages)
                      if contains_idr(bytes(message.data))), None)
    if first_idr is None or len(image_messages) - first_idr < 600:
        raise RuntimeError(f"not enough IDR-aligned images: first_idr={first_idr}, total={len(image_messages)}")
    image_index = 0
    for message, receive_us in image_messages[first_idr:first_idr + 600]:
            image_index += 1
            wrapper = pb.ImageMsgArray()
            wrapper.array.add().CopyFrom(message)
            data = wrapper.SerializeToString()
            path = image_dir / f"{image_index:06d}.bin"
            path.write_bytes(data)
            image_rows.append({"seq": message.header.seq, "stamp": message.header.stamp,
                               "format": int(message.format), "width": message.width,
                               "height": message.height, "size": len(data),
                               "sha256": hashlib.sha256(data).hexdigest(),
                               "receive_unix_us": receive_us,
                               "topic": "active/DEVICE00000001/image"})
    if image_index != 600:
        raise RuntimeError(f"expected 600 image messages, got {image_index}")
    with (image_dir / "metadata.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(image_rows[0]))
        writer.writeheader(); writer.writerows(image_rows)

    cloud_rows: list[dict] = []
    cloud_index = 0
    for packet_path in sorted((raw / "raw/raw/pointcloud").glob("*.bin")):
        packet_index = int(packet_path.stem)
        packet = pb.LidarPointMsgArray.FromString(packet_path.read_bytes())
        for message in packet.array:
            if cloud_index >= 200:
                break
            cloud_index += 1
            wrapper = pb.LidarPointMsgArray()
            wrapper.array.add().CopyFrom(message)
            data = wrapper.SerializeToString()
            path = cloud_dir / f"{cloud_index:06d}.bin"
            path.write_bytes(data)
            cloud_rows.append({"seq": message.header.seq, "stamp": message.header.stamp,
                               "points": len(message.points), "scaler": message.header.scaler,
                               "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                               "receive_unix_us": received[("pointcloud", packet_index)],
                               "topic": "active/DEVICE00000001/pointcloud"})
        if cloud_index >= 200:
            break
    if cloud_index != 200:
        raise RuntimeError(f"expected 200 pointcloud messages, got {cloud_index}")
    with (cloud_dir / "metadata.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(cloud_rows[0]))
        writer.writeheader(); writer.writerows(cloud_rows)


def copy_queries_and_embedded_config(raw: Path, output: Path) -> None:
    setting_dir = raw / "raw/queries/setting"
    config_dir = raw / "raw/queries/config_file"
    shutil.copy2(setting_dir / "request_001.bin", output / "setting_request.bin")
    shutil.copy2(setting_dir / "response_001.bin", output / "setting_response.bin")
    shutil.copy2(config_dir / "request_001.bin", output / "config_file_request.bin")
    response_path = config_dir / "response_001.bin"
    shutil.copy2(response_path, output / "config_file_response.bin")

    embedded = pb.FileMsgArray.FromString(response_path.read_bytes())
    config_output = output / "09_config"
    config_output.mkdir(exist_ok=True)
    # The live config_file response intentionally publishes the three board-owned
    # entities.  Camera intrinsics and lidar-camera extrinsics remain separately
    # supplied physical-calibration artifacts in the package template.
    expected = {"config.json", "calibration.json", "lidar_intrinsics.json"}
    seen = set()
    for message in embedded.array:
        name = Path(message.path).name
        if name in expected:
            (config_output / name).write_bytes(message.data)
            seen.add(name)
    if seen != expected:
        raise RuntimeError(f"config_file response missing files: {sorted(expected - seen)}")


def update_manual_evidence(raw: Path, output: Path, capture_manifest: dict) -> None:
    timing_source = raw / "dual_stream_600s.csv"
    timing_target = output / "manual_evidence/stability/dual_stream_610s.csv"
    shutil.copy2(timing_source, timing_target)
    evidence_path = output / "manual_evidence/evidence_manifest.json"
    evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    evidence["captured_at_utc"] = datetime.now(timezone.utc).isoformat()
    evidence["capture_session_id"] = "rk-live-acceptance"
    for item in evidence["items"]:
        if item["evidence_id"] == "time005-live-dual-stream-610s":
            item["started_at_utc"] = iso_utc(int(capture_manifest["started_unix_us"]))
            item["ended_at_utc"] = iso_utc(int(capture_manifest["ended_unix_us"]))
            item["description"] = (
                f"{capture_manifest['capture_seconds']:.1f}s fresh image+pointcloud continuous "
                f"stamp/receive recording; recorder dropped={capture_manifest['dropped']}"
            )
            item["sha256"] = sha256(timing_target)
    evidence_path.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def write_inventories(output: Path, capture_manifest: dict) -> None:
    entries = []
    for path in sorted(p for p in output.rglob("*") if p.is_file()):
        rel = path.relative_to(output).as_posix()
        if rel in {"manifest.json", "SHA256SUMS.txt"}:
            continue
        entries.append({"path": rel, "sha256": sha256(path)})
    manifest = {"label": "rk3562-fresh-live-acceptance-20260922",
                "sn": "DEVICE00000001",
                "capture_started_unix_us": int(capture_manifest["started_unix_us"]),
                "entries": entries}
    (output / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = []
    for path in sorted(p for p in output.rglob("*") if p.is_file() and p.name != "SHA256SUMS.txt"):
        lines.append(f"{sha256(path)}  {path.relative_to(output).as_posix()}")
    (output / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--sample-raw", type=Path,
                        help="optional short capture used for IDR-aligned payload samples and queries")
    parser.add_argument("--template", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit(f"refusing to overwrite {args.output}")
    args.output.mkdir(parents=True)
    long_capture = capture_dir(args.raw)
    sample_capture = capture_dir(args.sample_raw or args.raw)
    capture_manifest = json.loads((long_capture / "manifest.json").read_text(encoding="utf-8"))
    sample_manifest = json.loads((sample_capture / "manifest.json").read_text(encoding="utf-8"))
    if capture_manifest["dropped"] != 0:
        raise RuntimeError(f"live recorder dropped {capture_manifest['dropped']} packets")
    if sample_manifest["dropped"] != 0:
        raise RuntimeError(f"sample recorder dropped {sample_manifest['dropped']} packets")
    copy_static(args.template, args.output)
    split_payloads(sample_capture, args.output, sample_manifest)
    copy_queries_and_embedded_config(sample_capture, args.output)
    update_manual_evidence(long_capture, args.output, capture_manifest)
    for name in ("capture_610s.pcap", "capture_70s.pcap"):
        source = args.raw / name
        if source.stat().st_size < 1_000_000:
            raise RuntimeError(f"capture is unexpectedly small: {source}")
        shutil.copy2(source, args.output / name)
    write_inventories(args.output, capture_manifest)
    print(json.dumps({"output": str(args.output), "files": sum(1 for p in args.output.rglob('*') if p.is_file()),
                      "images": 600, "pointclouds": 200}, ensure_ascii=False))


if __name__ == "__main__":
    main()
