#!/usr/bin/env python3
"""Record exact Protobuf payloads received from one Zenoh device endpoint."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import queue
import shutil
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from PIL import Image
import zenoh
from pc import active_msgs_pb2 as pb


def sha256(data):
    return hashlib.sha256(data).hexdigest()


class Recorder:
    def __init__(self, channel, sn, endpoint, root):
        self.channel, self.sn, self.endpoint = channel, sn, endpoint
        self.root = root
        self.payload_dir = root / channel
        self.payload_dir.mkdir(parents=True)
        self.summary = (root / "decoded" / f"{channel}_summary.csv").open("w", newline="", encoding="utf-8")
        self.csv = csv.DictWriter(self.summary, fieldnames=(
            "index", "receive_timestamp_unix_us", "payload_size", "sha256", "seq", "source_stamp",
            "array_length", "format", "width", "height", "scaler", "points", "jpeg_soi_eoi", "jpeg_decodable", "error"))
        self.csv.writeheader()
        self.manifest, self.errors = [], []
        self.queue = queue.Queue(maxsize=256)
        self.running = True

    def receive(self, sample):
        try:
            self.queue.put_nowait((sample.payload.to_bytes(), time.time_ns() // 1000))
        except queue.Full:
            self.errors.append("receiver_queue_full")

    def decode(self, payload):
        row = {"array_length": "", "format": "", "width": "", "height": "", "scaler": "", "points": "", "jpeg_soi_eoi": "", "jpeg_decodable": "", "seq": "", "source_stamp": "", "error": ""}
        try:
            arr = pb.ImageMsgArray() if self.channel == "image" else pb.LidarPointMsgArray()
            arr.ParseFromString(payload)
            row["array_length"] = len(arr.array)
            if len(arr.array) != 1:
                row["error"] = "array_length_not_one"
                return row
            msg = arr.array[0]
            row["seq"], row["source_stamp"] = msg.header.seq, msg.header.stamp
            if self.channel == "image":
                row.update(format=msg.format, width=msg.width, height=msg.height)
                row["jpeg_soi_eoi"] = len(msg.data) >= 4 and msg.data[:2] == b"\xff\xd8" and msg.data[-2:] == b"\xff\xd9"
                try:
                    with Image.open(io.BytesIO(msg.data)) as image:
                        image.load()
                    row["jpeg_decodable"] = True
                except Exception as exc:
                    row["jpeg_decodable"] = False
                    row["error"] = f"jpeg_decode:{type(exc).__name__}"
            else:
                row.update(scaler=msg.header.scaler, points=len(msg.points))
        except Exception as exc:
            row["error"] = f"protobuf_parse:{type(exc).__name__}"
        return row

    def run(self, duration):
        config = zenoh.Config()
        config.insert_json5("mode", json.dumps("client"))
        config.insert_json5("connect/endpoints", json.dumps([self.endpoint]))
        config.insert_json5("scouting/multicast/enabled", "false")
        topic = f"active/{self.sn}/{self.channel}"
        with zenoh.open(config) as session:
            subscriber = session.declare_subscriber(topic, self.receive)
            deadline = time.monotonic() + duration
            index = 0
            while time.monotonic() < deadline:
                try:
                    payload, received_us = self.queue.get(timeout=.25)
                except queue.Empty:
                    continue
                index += 1
                filename = f"{index:06d}.bin"
                (self.payload_dir / filename).write_bytes(payload)
                row = self.decode(payload)
                row.update(index=index, receive_timestamp_unix_us=received_us, payload_size=len(payload), sha256=sha256(payload))
                self.csv.writerow(row)
                self.summary.flush()
                self.manifest.append({
                    "file": f"{self.channel}/{filename}", "topic": topic, "receive_timestamp_unix_us": received_us,
                    "payload_size": len(payload), "sha256": row["sha256"], "seq": row["seq"],
                    "source_stamp": row["source_stamp"], "format": row["format"], "scaler": row["scaler"]})
            subscriber.undeclare()
        self.summary.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sn", required=True)
    parser.add_argument("--endpoint", required=True, help="single device endpoint, e.g. tcp/203.0.113.179:7447")
    parser.add_argument("--seconds", type=float, default=300)
    parser.add_argument("--output", default=str(ROOT / "acceptance_data"))
    args = parser.parse_args()
    root = Path(args.output)
    if root.exists():
        raise SystemExit(f"Refusing to overwrite existing directory: {root}")
    (root / "decoded").mkdir(parents=True)
    recorders = [Recorder(kind, args.sn, args.endpoint, root) for kind in ("image", "pointcloud")]
    started = time.time_ns() // 1000
    threads = [threading.Thread(target=recorder.run, args=(args.seconds,), name=f"record-{recorder.channel}") for recorder in recorders]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    manifest = {"sn": args.sn, "endpoint": args.endpoint, "started_unix_us": started,
                "ended_unix_us": time.time_ns() // 1000, "records": sum((r.manifest for r in recorders), []),
                "errors": {r.channel: r.errors for r in recorders}}
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    with (root / "hashes.sha256").open("w", encoding="ascii") as hashes:
        for record in manifest["records"]:
            hashes.write(f"{record['sha256']}  {record['file']}\n")
    print(json.dumps({"output": str(root), "counts": {r.channel: len(r.manifest) for r in recorders}}, indent=2))


if __name__ == "__main__":
    main()
