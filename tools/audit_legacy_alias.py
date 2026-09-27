#!/usr/bin/env python3
"""Lightweight rate and payload audit for the legacy compatibility topics."""

import argparse
import json
import queue
import statistics
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import zenoh
from pc import active_msgs_pb2 as pb


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=15)
    parser.add_argument("--format", choices=("jpeg", "h264"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sn", default="LEGACY00000001")
    parser.add_argument("--endpoint", default="tcp/203.0.113.179:7447#bind=203.0.113.200:0")
    args = parser.parse_args()

    cfg = zenoh.Config()
    cfg.insert_json5("mode", '"client"')
    cfg.insert_json5("connect/endpoints", json.dumps([args.endpoint]))
    cfg.insert_json5("scouting/multicast/enabled", "false")
    incoming = queue.Queue(maxsize=4096)
    dropped = 0

    def receive(sample):
        nonlocal dropped
        try:
            incoming.put_nowait((str(sample.key_expr), time.time_ns() // 1000,
                                  sample.payload.to_bytes()))
        except queue.Full:
            dropped += 1

    image_items, image_puts, point_items, point_puts, imu_items = [], 0, [], 0, 0
    config = zenoh.open(cfg)
    with config as session:
        subscriber = session.declare_subscriber(f"active/{args.sn}/**", receive)
        # Let the direct TCP transport and subscriptions settle before timing.
        time.sleep(1)
        while True:
            try:
                incoming.get_nowait()
            except queue.Empty:
                break
        started = time.time_ns() // 1000
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            try:
                topic, received_us, payload = incoming.get(timeout=.2)
            except queue.Empty:
                continue
            if topic.endswith("/image"):
                array = pb.ImageMsgArray.FromString(payload)
                image_puts += 1
                for message in array.array:
                    data = bytes(message.data)
                    valid = ((args.format == "jpeg" and message.format == 2 and
                              data.startswith(b"\xff\xd8") and data.endswith(b"\xff\xd9")) or
                             (args.format == "h264" and message.format == 3 and
                              data.startswith((b"\x00\x00\x01", b"\x00\x00\x00\x01"))))
                    image_items.append((message.header.seq, message.header.stamp,
                                        received_us, len(array.array), valid))
            elif topic.endswith("/pointcloud"):
                array = pb.LidarPointMsgArray.FromString(payload)
                point_puts += 1
                for message in array.array:
                    point_items.append((message.header.seq, message.header.stamp,
                                        received_us, len(message.points), message.header.scaler))
            elif topic.endswith("/imu"):
                imu_items += len(pb.ImuMsgArray.FromString(payload).array)
        ended = time.time_ns() // 1000
        subscriber.undeclare()

    def rate(count):
        return count / ((ended - started) / 1_000_000)

    def gaps(rows):
        return sum(max(0, right[0] - left[0] - 1) for left, right in zip(rows, rows[1:]))

    result = {
        "seconds": (ended - started) / 1_000_000,
        "image": {
            "puts": image_puts, "frames": len(image_items), "put_hz": rate(image_puts),
            "frame_hz": rate(len(image_items)), "array_lengths": sorted(set(row[3] for row in image_items)),
            "seq_gaps": gaps(image_items), "payload_errors": sum(not row[4] for row in image_items),
            "epoch_skew_ms_median": statistics.median((row[2] - row[1]) / 1000 for row in image_items) if image_items else None,
        },
        "pointcloud": {
            "puts": point_puts, "frames": len(point_items), "hz": rate(len(point_items)),
            "seq_gaps": gaps(point_items), "scalers": sorted(set(row[4] for row in point_items)),
            "points_min": min((row[3] for row in point_items), default=0),
            "points_max": max((row[3] for row in point_items), default=0),
            "epoch_skew_ms_median": statistics.median((row[2] - row[1]) / 1000 for row in point_items) if point_items else None,
        },
        "imu_items": imu_items,
        "callback_drops": dropped,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
