#!/usr/bin/env python3
"""Build a vendor-acceptance 5.0.0 package from fresh RK live evidence."""
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


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def iso(us: int) -> str:
    return datetime.fromtimestamp(us / 1e6, timezone.utc).isoformat().replace("+00:00", "Z")


def dump_csv(path: Path, rows, fields) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader(); w.writerows(rows)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--capture", type=Path, required=True)
    ap.add_argument("--long-capture", type=Path, required=True)
    ap.add_argument("--pcap-70", type=Path, required=True)
    ap.add_argument("--pcap-610", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        raise SystemExit(f"refusing to overwrite {args.output}")
    for d in ("image_payload", "pointcloud_payload", "09_config", "manual_evidence/stability"):
        (args.output / d).mkdir(parents=True, exist_ok=True)
    sn = "DEVICE00000001"
    manifest = json.loads((args.capture / "manifest.json").read_text(encoding="utf-8"))
    rec = {(r["topic"].rsplit("/", 1)[-1], r["index"]): r for r in manifest["records"]}

    image_rows = []
    image_index = 0
    for src in sorted((args.capture / "raw/raw/image").glob("*.bin")):
        outer_index = int(src.stem)
        receive = rec[("image", outer_index)]["received_unix_us"]
        arr = pb.ImageMsgArray.FromString(src.read_bytes())
        for msg in arr.array:
            image_index += 1
            one = pb.ImageMsgArray(); one.array.append(msg)
            dst = args.output / "image_payload" / f"{image_index:06d}.bin"
            dst.write_bytes(one.SerializeToString())
            image_rows.append({"seq": msg.header.seq, "stamp": msg.header.stamp,
                               "format": msg.format, "width": msg.width, "height": msg.height,
                               "size": dst.stat().st_size, "sha256": sha(dst),
                               "receive_unix_us": receive, "topic": f"active/{sn}/image"})
    dump_csv(args.output / "image_payload/metadata.csv", image_rows,
             ["seq", "stamp", "format", "width", "height", "size", "sha256", "receive_unix_us", "topic"])

    cloud_rows = []
    for src in sorted((args.capture / "raw/raw/pointcloud").glob("*.bin")):
        index = int(src.stem)
        receive = rec[("pointcloud", index)]["received_unix_us"]
        arr = pb.LidarPointMsgArray.FromString(src.read_bytes())
        dst = args.output / "pointcloud_payload" / f"{index:06d}.bin"
        shutil.copy2(src, dst)
        msg = arr.array[0]
        cloud_rows.append({"seq": msg.header.seq, "stamp": msg.header.stamp,
                           "points": len(msg.points), "scaler": msg.header.scaler,
                           "size": dst.stat().st_size, "sha256": sha(dst),
                           "receive_unix_us": receive, "topic": f"active/{sn}/pointcloud"})
    dump_csv(args.output / "pointcloud_payload/metadata.csv", cloud_rows,
             ["seq", "stamp", "points", "scaler", "size", "sha256", "receive_unix_us", "topic"])

    cfg_q = args.capture / "raw/queries/config_file"
    set_q = args.capture / "raw/queries/setting"
    shutil.copy2(cfg_q / "request_001.bin", args.output / "config_file_request.bin")
    shutil.copy2(cfg_q / "response_001.bin", args.output / "config_file_response.bin")
    shutil.copy2(set_q / "request_001.bin", args.output / "setting_request.bin")
    shutil.copy2(set_q / "response_001.bin", args.output / "setting_response.bin")
    files = pb.FileMsgArray.FromString((cfg_q / "response_001.bin").read_bytes())
    embedded = {m.path: bytes(m.data) for m in files.array}
    for name in ("config.json", "calibration.json", "lidar_intrinsics.json"):
        (args.output / "09_config" / name).write_bytes(embedded[name])
    cal = json.loads(embedded["calibration.json"].decode("utf-8"))
    cam = cal["camera"]
    (args.output / "09_config/camera_intrinsics.json").write_text(json.dumps({
        "fx": cam["fx"], "fy": cam["fy"], "cx": cam["cx"], "cy": cam["cy"],
        "distortion": cam["distortion"], "device_sn": sn,
        "valid": bool(cal.get("valid")), "placeholder": cal.get("calibration_status") != "physical",
        "image_size": [cam["width"], cam["height"]], "source": "derived byte-for-byte live config_file calibration.json"
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    ext = cal["lidar_to_camera"]
    (args.output / "09_config/lidar_camera_extrinsics.json").write_text(json.dumps({
        "R": ext["R"], "t": ext["t"], "unit": ext.get("unit", "m"), "device_sn": sn,
        "valid": bool(cal.get("valid")), "placeholder": cal.get("calibration_status") != "physical",
        "source": "derived byte-for-byte live config_file calibration.json"
    }, ensure_ascii=False, indent=2), encoding="utf-8")

    shutil.copy2(ROOT / "protocol/active_msgs.proto", args.output / "active_msgs.proto")
    shutil.copy2(args.pcap_70, args.output / "capture_70s.pcap")
    shutil.copy2(args.pcap_610, args.output / "capture_610s.pcap")
    long_csv = args.long_capture / "dual_stream_600s.csv"
    long_dst = args.output / "manual_evidence/stability/dual_stream_610s.csv"
    shutil.copy2(long_csv, long_dst)
    long_rows = list(csv.DictReader(long_dst.open(encoding="utf-8")))
    times = [int(r["receive_unix_us"]) for r in long_rows]
    evidence = {"schema_version": "1.0", "captured_at_utc": iso((min(times) + max(times)) // 2),
                "device_serials": [sn], "capture_session_id": "rk-live-acceptance",
                "operator": "supplier-live-rerun", "items": [{
                    "evidence_id": "time005-live-dual-stream-610s", "rule_ids": ["TIME-005"],
                    "path": "stability/dual_stream_610s.csv", "sha256": sha(long_dst),
                    "device_serials": [sn], "started_at_utc": iso(min(times)),
                    "ended_at_utc": iso(max(times)), "raw": True,
                    "description": "Fresh live single-session image+pointcloud seq/stamp/receive record",
                    "scenario": "long_term"}]}
    (args.output / "manual_evidence/evidence_manifest.json").write_text(
        json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
    (args.output / "COORDINATE_SYSTEM.md").write_text(
        "# RK3562 live coordinate declaration\n\n"
        "Device config declares X forward, Y left, Z up, right-handed, metre = raw/scaler. "
        "This live rerun does not convert the declaration into physical proof.\n", encoding="utf-8")
    (args.output / "scenario_notes.md").write_text(
        "# Fresh live rerun\n\n"
        "One RK3562 was captured live. 70s and >=600s board-side AF_PACKET captures were used. "
        "Three-device concurrency was not available. Reboot evidence is recorded separately if present.\n",
        encoding="utf-8")

    entries = []
    for p in sorted(args.output.rglob("*")):
        if p.is_file() and p.name not in {"manifest.json", "SHA256SUMS.txt"}:
            entries.append({"path": p.relative_to(args.output).as_posix(), "sha256": sha(p)})
    (args.output / "manifest.json").write_text(json.dumps({
        "label": "rk3562-fresh-live-acceptance-20260920", "sn": sn,
        "capture_started_unix_us": manifest["started_unix_us"], "entries": entries
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    lines = []
    for p in sorted(args.output.rglob("*")):
        if p.is_file() and p.name != "SHA256SUMS.txt":
            lines.append(f"{sha(p)}  {p.relative_to(args.output).as_posix()}")
    (args.output / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({"image_frames": len(image_rows), "cloud_frames": len(cloud_rows),
                      "long_rows": len(long_rows), "files": len(lines)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
