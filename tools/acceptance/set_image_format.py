#!/usr/bin/env python3
"""Change the protocol image format through the device's Zenoh setting query."""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import zenoh
from pc import active_msgs_pb2 as pb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--endpoint", required=True)
    ap.add_argument("--sn", required=True)
    ap.add_argument("--format", choices=("h264", "jpeg"), required=True)
    args = ap.parse_args()
    value = 1 if args.format == "h264" else 2
    cfg = zenoh.Config()
    cfg.insert_json5("mode", '"client"')
    cfg.insert_json5("connect/endpoints", json.dumps([args.endpoint]))
    cfg.insert_json5("scouting/multicast/enabled", "false")
    request = pb.SettingRequest(header=pb.Header(seq=1), image_format=value)
    with zenoh.open(cfg) as session:
        replies = [reply.ok.payload.to_bytes() for reply in session.get(
            f"active/{args.sn}/cmd/setting", payload=request.SerializeToString(),
            timeout=5) if reply.ok]
    if len(replies) != 1:
        raise RuntimeError(f"expected one setting reply, got {len(replies)}")
    response = pb.SettingResponse.FromString(replies[0])
    if response.error.code or response.image_format != value:
        raise RuntimeError(f"setting rejected: {response}")
    print(json.dumps({"status": "OK", "image_format": args.format,
                      "setting_value": response.image_format}, indent=2))


if __name__ == "__main__":
    main()
