#!/usr/bin/env python3
"""Disconnect and reconnect one Zenoh client; measure dual-topic and IDR recovery."""
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
from tools.acceptance.h264_check import nals


def sample_session(endpoint: str, sn: str, seconds: float) -> dict:
    cfg = zenoh.Config()
    cfg.insert_json5("mode", '"client"')
    cfg.insert_json5("connect/endpoints", json.dumps([endpoint]))
    cfg.insert_json5("scouting/multicast/enabled", "false")
    pending = queue.Queue(maxsize=4096)
    dropped = 0

    def receive(sample):
        nonlocal dropped
        try:
            pending.put_nowait((time.monotonic(), str(sample.key_expr), sample.payload.to_bytes()))
        except queue.Full:
            dropped += 1

    opened = time.monotonic()
    with zenoh.open(cfg) as session:
        image = session.declare_subscriber(f"active/{sn}/image", receive)
        cloud = session.declare_subscriber(f"active/{sn}/pointcloud", receive)
        subscribed = time.monotonic()
        deadline = subscribed + seconds
        counts = {"image": 0, "pointcloud": 0}
        first_topic_s = {}
        first_decodable_s = None
        first_stamp = {}
        last_stamp = {}
        seq_gaps = {"image": 0, "pointcloud": 0}
        prior_seq = {}
        while time.monotonic() < deadline:
            try:
                received, key, raw = pending.get(timeout=min(0.5, max(0.01, deadline - time.monotonic())))
            except queue.Empty:
                continue
            kind = key.rsplit("/", 1)[-1]
            if kind == "image":
                items = pb.ImageMsgArray.FromString(raw).array
            elif kind == "pointcloud":
                items = pb.LidarPointMsgArray.FromString(raw).array
            else:
                continue
            first_topic_s.setdefault(kind, round(received - subscribed, 3))
            for msg in items:
                counts[kind] += 1
                stamp = msg.header.stamp
                first_stamp.setdefault(kind, stamp)
                if kind in last_stamp and stamp <= last_stamp[kind]:
                    seq_gaps[kind] += 1
                last_stamp[kind] = stamp
                if kind in prior_seq and msg.header.seq > prior_seq[kind] + 1:
                    seq_gaps[kind] += msg.header.seq - prior_seq[kind] - 1
                prior_seq[kind] = msg.header.seq
                if kind == "image" and first_decodable_s is None:
                    types = [unit[0] & 31 for _, unit in nals(bytes(msg.data))]
                    if {5, 7, 8}.issubset(types):
                        first_decodable_s = round(received - subscribed, 3)
        image.undeclare()
        cloud.undeclare()
    return {"connect_s": round(subscribed - opened, 3), "counts": counts,
            "first_topic_s": first_topic_s, "first_decodable_h264_s": first_decodable_s,
            "first_stamp_us": first_stamp, "last_stamp_us": last_stamp,
            "seq_gap_or_non_increasing": seq_gaps, "callback_dropped": dropped}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--endpoint", required=True)
    ap.add_argument("--sn", required=True)
    ap.add_argument("--before-seconds", type=float, default=3)
    ap.add_argument("--offline-seconds", type=float, default=2)
    ap.add_argument("--after-seconds", type=float, default=5)
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    before = sample_session(args.endpoint, args.sn, args.before_seconds)
    time.sleep(args.offline_seconds)
    after = sample_session(args.endpoint, args.sn, args.after_seconds)
    errors = []
    for label, trial in (("before", before), ("after", after)):
        if min(trial["counts"].values()) == 0:
            errors.append(f"{label}: one topic did not recover")
        if trial["callback_dropped"] or any(trial["seq_gap_or_non_increasing"].values()):
            errors.append(f"{label}: drop, sequence gap, or stamp regression")
    if after["first_decodable_h264_s"] is None or after["first_decodable_h264_s"] > 1:
        errors.append("first independently decodable H264 AU after reconnect > 1s")
    for kind in ("image", "pointcloud"):
        if before["last_stamp_us"].get(kind, 0) >= after["first_stamp_us"].get(kind, 0):
            errors.append(f"{kind}: stale replay or non-increasing stamp across reconnect")
    result = {"status": "PASS" if not errors else "FAIL", "before": before,
              "offline_seconds": args.offline_seconds, "after": after, "errors": errors}
    rendered = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
