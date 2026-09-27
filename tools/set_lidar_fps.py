#!/usr/bin/env python3
"""Persist an XT-M60 point-cloud output rate (1..30 Hz) through the protocol setting API."""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import zenoh
from pc import active_msgs_pb2 as pb

SN = "DEVICE00000001"
ENDPOINT = "tcp/198.51.100.179:7447#bind=198.51.100.200:0"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fps", type=int, choices=range(1, 31), required=True)
    parser.add_argument("--image-fps", type=int, choices=range(5, 61), default=None)
    parser.add_argument("--h264-gop", type=int, choices=range(1, 201), default=None)
    parser.add_argument("--endpoint", default=ENDPOINT)
    args = parser.parse_args()
    fields = {"header": pb.Header(seq=1), "lidar_fps": args.fps}
    if args.image_fps is not None:
        fields["image_fps"] = args.image_fps
    if args.h264_gop is not None:
        fields["h264_gop"] = args.h264_gop
    request = pb.SettingRequest(**fields)
    config = zenoh.Config()
    config.insert_json5("mode", '"client"')
    config.insert_json5("connect/endpoints", json.dumps([args.endpoint]))
    config.insert_json5("scouting/multicast/enabled", "false")
    with zenoh.open(config) as session:
        replies = [pb.SettingResponse.FromString(reply.ok.payload.to_bytes()) for reply in session.get(
            f"active/{SN}/cmd/setting", payload=request.SerializeToString(), timeout=5) if reply.ok]
    if len(replies) != 1:
        raise SystemExit(f"expected one response, got {len(replies)}")
    response = replies[0]
    print(json.dumps({"error_code": response.error.code, "error": response.error.desc,
                      "lidar_fps": response.lidar_fps, "image_fps": response.image_fps,
                      "h264_gop": response.h264_gop,
                      "parameter": response.parameter}, ensure_ascii=False, indent=2))
    if response.error.code != 0 or response.lidar_fps != args.fps:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
