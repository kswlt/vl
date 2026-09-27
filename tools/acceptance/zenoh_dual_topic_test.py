#!/usr/bin/env python3
"""Subscribe to image and pointcloud through one Zenoh client session."""
from __future__ import annotations

import argparse
import json
import queue
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
import zenoh
from pc import active_msgs_pb2 as pb


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--sn", required=True)
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")

    cfg = zenoh.Config()
    cfg.insert_json5("mode", '"client"')
    cfg.insert_json5("connect/endpoints", json.dumps([args.endpoint]))
    cfg.insert_json5("scouting/multicast/enabled", "false")
    incoming = queue.Queue(maxsize=4096)
    counts = {"image": 0, "pointcloud": 0}
    packets = {"image": 0, "pointcloud": 0}
    dropped = 0
    errors = []

    def receive(sample):
        nonlocal dropped
        kind = str(sample.key_expr).rsplit("/", 1)[-1]
        if kind not in counts:
            return
        try:
            incoming.put_nowait((kind, sample.payload.to_bytes()))
        except queue.Full:
            dropped += 1

    started = time.monotonic()
    with zenoh.open(cfg) as session:
        image = session.declare_subscriber(f"active/{args.sn}/image", receive)
        cloud = session.declare_subscriber(f"active/{args.sn}/pointcloud", receive)
        deadline = started + args.seconds
        while time.monotonic() < deadline:
            try:
                kind, payload = incoming.get(timeout=min(0.5, max(0.01, deadline - time.monotonic())))
            except queue.Empty:
                continue
            packets[kind] += 1
            try:
                if kind == "image":
                    items = pb.ImageMsgArray.FromString(payload).array
                    if any(m.format != pb.ImageFormatH264 or m.width != 1920 or m.height != 1080 for m in items):
                        errors.append("image format or resolution mismatch")
                else:
                    items = pb.LidarPointMsgArray.FromString(payload).array
                    if any(m.header.scaler != 1000 for m in items):
                        errors.append("pointcloud scaler mismatch")
                counts[kind] += len(items)
            except Exception as exc:
                errors.append(f"{kind} decode: {exc}")
        image.undeclare()
        cloud.undeclare()

    elapsed = time.monotonic() - started
    if dropped:
        errors.append(f"callback queue dropped {dropped}")
    if not counts["image"] or not counts["pointcloud"]:
        errors.append("both topics were not received")
    result = {"status": "PASS" if not errors else "FAIL", "endpoint": args.endpoint,
              "sn": args.sn, "client_sessions": 1, "subscriptions": 2,
              "elapsed_s": round(elapsed, 3), "packets": packets, "frames": counts,
              "fps": {kind: round(value / elapsed, 3) for kind, value in counts.items()},
              "callback_dropped": dropped, "errors": errors[:20]}
    rendered = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
