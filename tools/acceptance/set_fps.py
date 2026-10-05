#!/usr/bin/env python3
"""Set lidar/image fps through the device's Zenoh setting query (zigzag-encoded)."""

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
    ap.add_argument("--target", choices=("image_fps", "lidar_fps"), required=True)
    ap.add_argument("--value", type=int, required=True)
    args = ap.parse_args()
    cfg = zenoh.Config()
    cfg.insert_json5("mode", '"client"')
    cfg.insert_json5("connect/endpoints", json.dumps([args.endpoint]))
    cfg.insert_json5("scouting/multicast/enabled", "false")
    kw = {args.target: args.value}
    request = pb.SettingRequest(header=pb.Header(seq=1), **kw)
    with zenoh.open(cfg) as session:
        replies = [reply.ok.payload.to_bytes() for reply in session.get(
            f"active/{args.sn}/cmd/setting", payload=request.SerializeToString(),
            timeout=5) if reply.ok]
    if len(replies) != 1:
        raise RuntimeError(f"expected one setting reply, got {len(replies)}")
    response = pb.SettingResponse.FromString(replies[0])
    got = getattr(response, args.target, None)
    print(json.dumps({
        "status": "OK" if (not response.error.code and got == args.value) else "REJECTED",
        "error_code": response.error.code,
        "requested": {args.target: args.value},
        "echoed": {args.target: got},
        "full_response": str(response),
    }, indent=2, ensure_ascii=False))
    if response.error.code or got != args.value:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
