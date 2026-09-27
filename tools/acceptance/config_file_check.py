#!/usr/bin/env python3
"""Read and validate the live config_file query without changing settings."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
import zenoh
from pc import active_msgs_pb2 as pb


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--endpoint", required=True)
    ap.add_argument("--sn", required=True)
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    cfg = zenoh.Config()
    cfg.insert_json5("mode", '"client"')
    cfg.insert_json5("connect/endpoints", json.dumps([args.endpoint]))
    cfg.insert_json5("scouting/multicast/enabled", "false")
    errors = []
    with zenoh.open(cfg) as session:
        replies = [r.ok.payload.to_bytes() for r in session.get(
            f"active/{args.sn}/config_file", payload=b"", timeout=5) if r.ok]
    if len(replies) != 1:
        errors.append(f"expected one config_file reply, got {len(replies)}")
    files = {}
    if replies:
        for msg in pb.FileMsgArray.FromString(replies[0]).array:
            if msg.path in files:
                errors.append(f"duplicate path {msg.path}")
            files[msg.path] = bytes(msg.data)
    for name in ("config.json", "calibration.json"):
        if name not in files:
            errors.append(f"missing {name}")
    config = {}
    calibration = {}
    try:
        if "config.json" in files:
            config = json.loads(files["config.json"])
        if "calibration.json" in files:
            calibration = json.loads(files["calibration.json"])
    except (ValueError, UnicodeDecodeError) as exc:
        errors.append(f"invalid JSON: {exc}")
    sensors = config.get("sensors", {})
    compat = config.get("legacy_compatibility", {})
    if compat.get("legacy_topics_only") and compat.get("legacy_sn") != args.sn:
        errors.append("queried SN differs from configured legacy SN")
    if sensors.get("image_format") not in (1, 3):
        errors.append("not configured for H264")
    if config.get("timestamp_mode") != "unix_epoch":
        errors.append("timestamp_mode is not unix_epoch")
    result = {"status": "PASS" if not errors else "FAIL", "paths": sorted(files),
              "query_sn": args.sn, "configured_sn": compat.get("legacy_sn"),
              "image_format_config": sensors.get("image_format"),
              "image_fps_setting": sensors.get("image_fps"),
              "lidar_fps_setting": sensors.get("lidar_fps"),
              "timestamp_mode": config.get("timestamp_mode"),
              "calibration_version": calibration.get("version"),
              "missing_recommended_metadata": ["firmware_version", "software_git_version",
                                               "zenoh_version", "listener_endpoint", "timestamp_unit", "scaler"],
              "errors": errors}
    rendered = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
