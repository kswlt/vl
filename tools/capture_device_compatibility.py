#!/usr/bin/env python3
"""Capture all public Active-protocol traffic from one device without changing it."""

import argparse
import collections
import hashlib
import json
import queue
import re
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import zenoh
from google.protobuf.json_format import MessageToDict
from pc import active_msgs_pb2 as pb


def safe_component(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_") or "unknown"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def h264_nal_types(data: bytes):
    found = []
    i = 0
    while i + 4 <= len(data):
        if data[i : i + 3] == b"\x00\x00\x01":
            pos = i + 3
            i = pos + 1
        elif data[i : i + 4] == b"\x00\x00\x00\x01":
            pos = i + 4
            i = pos + 1
        else:
            i += 1
            continue
        if pos < len(data):
            found.append(data[pos] & 0x1F)
    return found


def decode_payload(topic: str, payload: bytes):
    suffix = topic.rsplit("/", 1)[-1]
    result = {"decode": "unknown"}
    if suffix == "image":
        arr = pb.ImageMsgArray.FromString(payload)
        result = {"decode": "ImageMsgArray", "array_length": len(arr.array), "items": []}
        for msg in arr.array:
            data = bytes(msg.data)
            result["items"].append({
                "seq": msg.header.seq, "stamp": msg.header.stamp, "scaler": msg.header.scaler,
                "format": msg.format, "width": msg.width, "height": msg.height,
                "data_bytes": len(data), "data_sha256": sha256(data),
                "jpeg_soi": data.startswith(b"\xff\xd8"), "jpeg_eoi": data.endswith(b"\xff\xd9"),
                "h264_annexb": data.startswith(b"\x00\x00\x01") or data.startswith(b"\x00\x00\x00\x01"),
                "h264_nal_types": h264_nal_types(data) if msg.format == 3 else [],
            })
    elif suffix == "pointcloud":
        arr = pb.LidarPointMsgArray.FromString(payload)
        result = {"decode": "LidarPointMsgArray", "array_length": len(arr.array), "items": []}
        for msg in arr.array:
            rings = collections.Counter(p.ring for p in msg.points)
            offsets = collections.Counter(p.offset for p in msg.points)
            result["items"].append({
                "seq": msg.header.seq, "stamp": msg.header.stamp, "scaler": msg.header.scaler,
                "points": len(msg.points),
                "x_min": min((p.x for p in msg.points), default=0), "x_max": max((p.x for p in msg.points), default=0),
                "y_min": min((p.y for p in msg.points), default=0), "y_max": max((p.y for p in msg.points), default=0),
                "z_min": min((p.z for p in msg.points), default=0), "z_max": max((p.z for p in msg.points), default=0),
                "rgbi_min": min((p.rgbi for p in msg.points), default=0), "rgbi_max": max((p.rgbi for p in msg.points), default=0),
                "rings": dict(rings), "offsets_min": min(offsets, default=0), "offsets_max": max(offsets, default=0),
            })
    elif suffix == "imu":
        arr = pb.ImuMsgArray.FromString(payload)
        result = {"decode": "ImuMsgArray", "array_length": len(arr.array), "items": [
            {"seq": msg.header.seq, "stamp": msg.header.stamp, "scaler": msg.header.scaler,
             "ax": msg.ax, "ay": msg.ay, "az": msg.az, "gx": msg.gx, "gy": msg.gy, "gz": msg.gz}
            for msg in arr.array
        ]}
    elif suffix == "config_file":
        arr = pb.FileMsgArray.FromString(payload)
        result = {"decode": "FileMsgArray", "array_length": len(arr.array), "items": [
            {"seq": msg.header.seq, "stamp": msg.header.stamp, "scaler": msg.header.scaler,
             "path": msg.path, "data_bytes": len(msg.data), "data_sha256": sha256(msg.data)}
            for msg in arr.array
        ]}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True)
    parser.add_argument("--sn", required=True)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit(f"refusing to overwrite {args.output}")
    raw_root = args.output / "raw"
    raw_root.mkdir(parents=True)

    config = zenoh.Config()
    config.insert_json5("mode", '"client"')
    config.insert_json5("connect/endpoints", json.dumps([args.endpoint]))
    config.insert_json5("scouting/multicast/enabled", "false")

    incoming = queue.Queue(maxsize=8192)
    dropped = 0
    records = []
    topic_counts = collections.Counter()
    stop = threading.Event()

    def receive(sample):
        nonlocal dropped
        try:
            incoming.put_nowait((str(sample.key_expr), sample.payload.to_bytes(), time.time_ns() // 1000))
        except queue.Full:
            dropped += 1

    def writer():
        while not stop.is_set() or not incoming.empty():
            try:
                topic, payload, received_us = incoming.get(timeout=0.2)
            except queue.Empty:
                continue
            suffix = safe_component(topic.rsplit("/", 1)[-1])
            topic_counts[topic] += 1
            index = topic_counts[topic]
            relative = Path("raw") / suffix / f"{index:06d}.bin"
            destination = args.output / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(payload)
            record = {
                "topic": topic, "index": index, "file": relative.as_posix(),
                "received_unix_us": received_us, "payload_bytes": len(payload), "payload_sha256": sha256(payload),
            }
            try:
                record.update(decode_payload(topic, payload))
            except Exception as exc:
                record.update(decode="error", decode_error=f"{type(exc).__name__}: {exc}")
            records.append(record)

    started_us = time.time_ns() // 1000
    worker = threading.Thread(target=writer, name="compat-writer")
    worker.start()
    query_results = {}
    with zenoh.open(config) as session:
        subscriber = session.declare_subscriber(f"active/{args.sn}/**", receive)
        time.sleep(args.seconds)
        subscriber.undeclare()

        read_setting = pb.SettingRequest(header=pb.Header(seq=0)).SerializeToString()
        replies = [r.ok.payload.to_bytes() for r in session.get(
            f"active/{args.sn}/cmd/setting", payload=read_setting, timeout=5) if r.ok]
        query_results["setting"] = []
        for index, payload in enumerate(replies, 1):
            path = Path("queries") / "setting" / f"response_{index:03d}.bin"
            (args.output / path).parent.mkdir(parents=True, exist_ok=True)
            (args.output / path).write_bytes(payload)
            try:
                decoded = MessageToDict(pb.SettingResponse.FromString(payload), preserving_proto_field_name=True)
            except Exception as exc:
                decoded = {"decode_error": f"{type(exc).__name__}: {exc}"}
            query_results["setting"].append({"file": path.as_posix(), "bytes": len(payload), "sha256": sha256(payload), "decoded": decoded})

        replies = [r.ok.payload.to_bytes() for r in session.get(
            f"active/{args.sn}/config_file", payload=b"", timeout=5) if r.ok]
        query_results["config_file"] = []
        for index, payload in enumerate(replies, 1):
            path = Path("queries") / "config_file" / f"response_{index:03d}.bin"
            (args.output / path).parent.mkdir(parents=True, exist_ok=True)
            (args.output / path).write_bytes(payload)
            try:
                arr = pb.FileMsgArray.FromString(payload)
                decoded = [{"path": m.path, "seq": m.header.seq, "stamp": m.header.stamp,
                            "scaler": m.header.scaler, "bytes": len(m.data), "sha256": sha256(m.data)} for m in arr.array]
            except Exception as exc:
                decoded = [{"decode_error": f"{type(exc).__name__}: {exc}"}]
            query_results["config_file"].append({"file": path.as_posix(), "bytes": len(payload), "sha256": sha256(payload), "decoded": decoded})

    stop.set()
    worker.join()
    ended_us = time.time_ns() // 1000
    records.sort(key=lambda row: row["received_unix_us"])
    manifest = {
        "label": args.label, "sn": args.sn, "endpoint": args.endpoint,
        "started_unix_us": started_us, "ended_unix_us": ended_us,
        "capture_seconds": args.seconds, "dropped": dropped,
        "topic_counts": dict(topic_counts), "query_results": query_results, "records": records,
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "topic_counts": dict(topic_counts),
                      "dropped": dropped, "query_counts": {k: len(v) for k, v in query_results.items()}}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
