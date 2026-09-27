#!/usr/bin/env python3
"""Reboot the configured RK board through the Active protocol."""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import zenoh
from pc import active_msgs_pb2 as pb


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sn", default="VENDOR00000001")
    parser.add_argument(
        "--endpoint",
        default="tcp/203.0.113.179:7447#bind=203.0.113.200:0",
    )
    parser.add_argument("--seq", type=int, default=1)
    args = parser.parse_args()

    config = zenoh.Config()
    config.insert_json5("mode", '"client"')
    config.insert_json5("connect/endpoints", json.dumps([args.endpoint]))
    config.insert_json5("scouting/multicast/enabled", "false")
    request = pb.RebootRequest(header=pb.Header(seq=args.seq)).SerializeToString()
    with zenoh.open(config) as session:
        replies = [
            pb.RebootResponse.FromString(reply.ok.payload.to_bytes())
            for reply in session.get(
                f"active/{args.sn}/cmd/reboot", payload=request, timeout=5
            )
            if reply.ok
        ]
    if len(replies) != 1:
        raise SystemExit(f"expected one response, got {len(replies)}")
    response = replies[0]
    print(json.dumps({"error_code": response.error.code, "error": response.error.desc}))
    if response.error.code != 0:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
