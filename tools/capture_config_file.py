#!/usr/bin/env python3
"""Read and retain the current config_file response without changing board state."""
import argparse
import csv
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import zenoh
from google.protobuf.json_format import MessageToDict
from pc import active_msgs_pb2 as pb

SN = "VENDOR00000001"
ENDPOINT = "tcp/203.0.113.179:7447#bind=203.0.113.200:0"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", required=True)
    parser.add_argument("--endpoint", default=ENDPOINT)
    args = parser.parse_args()
    root = Path(args.package)
    raw_dir, config_dir = root / "02_raw_capture/config_payload", root / "09_config"
    raw_dir.mkdir(parents=True, exist_ok=True); config_dir.mkdir(parents=True, exist_ok=True)
    cfg = zenoh.Config(); cfg.insert_json5("mode", '"client"'); cfg.insert_json5("connect/endpoints", json.dumps([args.endpoint])); cfg.insert_json5("scouting/multicast/enabled", "false")
    with zenoh.open(cfg) as session:
        replies = [reply.ok.payload.to_bytes() for reply in session.get(f"active/{SN}/config_file", payload=b"", timeout=5) if reply.ok]
    if len(replies) != 1:
        raise SystemExit(f"expected one config_file reply, received {len(replies)}")
    raw = replies[0]; array = pb.FileMsgArray.FromString(raw)
    (raw_dir / "config_file_response.bin").write_bytes(raw)
    (raw_dir / "config_file_request.bin").write_bytes(b"")
    rows = []
    for item in array.array:
        safe_name = Path(item.path).name
        if safe_name not in {"config.json", "calibration.json", "lidar_intrinsics.json"}:
            continue
        (config_dir / safe_name).write_bytes(item.data)
        rows.append({"filename": safe_name, "seq": item.header.seq, "stamp_us": item.header.stamp,
                     "bytes": len(item.data), "sha256": hashlib.sha256(item.data).hexdigest(),
                     "protobuf": MessageToDict(item, preserving_proto_field_name=True)})
    (raw_dir / "metadata.json").write_text(json.dumps({"topic": f"active/{SN}/config_file", "response_sha256": hashlib.sha256(raw).hexdigest(), "files": rows}, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (config_dir / "device_sn.txt").write_text(SN + "\n", encoding="ascii")
    print(json.dumps({"files": [r["filename"] for r in rows], "response_bytes": len(raw)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
