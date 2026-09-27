#!/usr/bin/env python3
"""Capture one setting request/reply pair; format 0 is read-only/no-change."""
import argparse
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
    parser = argparse.ArgumentParser(); parser.add_argument("--package", required=True); parser.add_argument("--format", type=int, default=0); parser.add_argument("--endpoint", default=ENDPOINT); parser.add_argument("--sn", default=SN); args = parser.parse_args()
    request = pb.SettingRequest(header=pb.Header(seq=1), image_format=args.format).SerializeToString()
    cfg = zenoh.Config(); cfg.insert_json5("mode", '"client"'); cfg.insert_json5("connect/endpoints", json.dumps([args.endpoint])); cfg.insert_json5("scouting/multicast/enabled", "false")
    with zenoh.open(cfg) as session:
        replies = [reply.ok.payload.to_bytes() for reply in session.get(f"active/{args.sn}/cmd/setting", payload=request, timeout=5) if reply.ok]
    if len(replies) != 1: raise SystemExit(f"expected one reply, got {len(replies)}")
    response = pb.SettingResponse.FromString(replies[0])
    target = Path(args.package) / "02_raw_capture/setting_payload"; target.mkdir(parents=True, exist_ok=True)
    stem = f"setting_image_format_{args.format}"
    (target / f"{stem}_request.bin").write_bytes(request); (target / f"{stem}_response.bin").write_bytes(replies[0])
    (target / f"{stem}_metadata.json").write_text(json.dumps({"request_sha256": hashlib.sha256(request).hexdigest(), "response_sha256": hashlib.sha256(replies[0]).hexdigest(), "response": MessageToDict(response, preserving_proto_field_name=True)}, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"error_code": response.error.code, "image_format": response.image_format, "stamp_us": response.header.stamp}))

if __name__ == "__main__": main()
