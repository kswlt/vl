#!/usr/bin/env python3
"""Read one device's current setting response, including query errors."""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import zenoh
from google.protobuf.json_format import MessageToDict
from pc import active_msgs_pb2 as pb


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sn", required=True)
    parser.add_argument("--endpoint", default="tcp/203.0.113.179:7447#bind=203.0.113.200:0")
    args = parser.parse_args()
    cfg = zenoh.Config()
    cfg.insert_json5("mode", '"client"')
    cfg.insert_json5("connect/endpoints", json.dumps([args.endpoint]))
    cfg.insert_json5("scouting/multicast/enabled", "false")
    request = pb.SettingRequest(header=pb.Header(seq=0)).SerializeToString()
    rows = []
    with zenoh.open(cfg) as session:
        for reply in session.get(f"active/{args.sn}/cmd/setting", payload=request, timeout=5):
            if reply.ok:
                response = pb.SettingResponse.FromString(reply.ok.payload.to_bytes())
                rows.append({"ok": MessageToDict(response, preserving_proto_field_name=True)})
            else:
                rows.append({"error": str(reply.err)})
    print(json.dumps(rows, indent=2, ensure_ascii=False))
    if not any("ok" in row for row in rows):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
