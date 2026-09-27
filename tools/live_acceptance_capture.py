#!/usr/bin/env python3
"""Single-session RK3562 acceptance capture.

Records every image/pointcloud header for long-term timing while retaining the
first requested number of original Zenoh payloads.  The same session performs
read-only setting and config_file queries, avoiding duplicate downlink streams
in the accompanying packet capture.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import queue
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import zenoh
from google.protobuf.json_format import MessageToDict
from pc import active_msgs_pb2 as pb


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--endpoint", default="tcp/203.0.113.179:7447#bind=203.0.113.200:0")
    ap.add_argument("--sn", default="DEVICE00000001")
    ap.add_argument("--seconds", type=float, default=610.0)
    ap.add_argument("--raw-packets", type=int, default=200)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        raise SystemExit(f"refusing to overwrite {args.output}")
    args.output.mkdir(parents=True)
    for kind in ("image", "pointcloud"):
        (args.output / "raw" / "raw" / kind).mkdir(parents=True)

    cfg = zenoh.Config()
    cfg.insert_json5("mode", '"client"')
    cfg.insert_json5("connect/endpoints", json.dumps([args.endpoint]))
    cfg.insert_json5("scouting/multicast/enabled", "false")

    incoming: queue.Queue = queue.Queue(maxsize=32768)
    stop = threading.Event()
    dropped = 0
    packet_counts = {"image": 0, "pointcloud": 0}
    item_counts = {"image": 0, "pointcloud": 0}
    records = []
    timing_rows = []

    def receive(sample):
        nonlocal dropped
        kind = str(sample.key_expr).rsplit("/", 1)[-1]
        if kind not in packet_counts:
            return
        try:
            incoming.put_nowait((kind, sample.payload.to_bytes(), time.time_ns() // 1000))
        except queue.Full:
            dropped += 1

    def writer():
        while not stop.is_set() or not incoming.empty():
            try:
                kind, raw, recv_us = incoming.get(timeout=0.2)
            except queue.Empty:
                continue
            packet_counts[kind] += 1
            index = packet_counts[kind]
            if kind == "image":
                array = pb.ImageMsgArray.FromString(raw)
                details = [{"seq": m.header.seq, "stamp": m.header.stamp,
                            "scaler": m.header.scaler, "format": m.format,
                            "width": m.width, "height": m.height,
                            "data_bytes": len(m.data)} for m in array.array]
            else:
                array = pb.LidarPointMsgArray.FromString(raw)
                details = [{"seq": m.header.seq, "stamp": m.header.stamp,
                            "scaler": m.header.scaler, "points": len(m.points)}
                           for m in array.array]
            for item in details:
                item_counts[kind] += 1
                timing_rows.append({"receive_unix_us": recv_us, "stream": kind,
                                    "seq": item["seq"], "stamp": item["stamp"],
                                    "scaler": item["scaler"]})
            if index <= args.raw_packets:
                rel = Path("raw") / "raw" / kind / f"{index:06d}.bin"
                (args.output / rel).write_bytes(raw)
                records.append({"topic": f"active/{args.sn}/{kind}", "index": index,
                                "file": rel.as_posix(), "received_unix_us": recv_us,
                                "payload_bytes": len(raw), "payload_sha256": digest(raw),
                                "array_length": len(details), "items": details})

    started_us = time.time_ns() // 1000
    worker = threading.Thread(target=writer, name="acceptance-writer")
    worker.start()
    query_results = {}
    with zenoh.open(cfg) as session:
        sub = session.declare_subscriber(f"active/{args.sn}/**", receive)
        time.sleep(args.seconds)
        sub.undeclare()

        request = pb.SettingRequest(header=pb.Header(seq=1), image_format=0).SerializeToString()
        replies = [r.ok.payload.to_bytes() for r in session.get(
            f"active/{args.sn}/cmd/setting", payload=request, timeout=5) if r.ok]
        query_results["setting"] = []
        qdir = args.output / "raw" / "queries" / "setting"
        qdir.mkdir(parents=True)
        (qdir / "request_001.bin").write_bytes(request)
        for i, raw in enumerate(replies, 1):
            (qdir / f"response_{i:03d}.bin").write_bytes(raw)
            msg = pb.SettingResponse.FromString(raw)
            query_results["setting"].append({"file": f"raw/queries/setting/response_{i:03d}.bin",
                                              "bytes": len(raw), "sha256": digest(raw),
                                              "decoded": MessageToDict(msg, preserving_proto_field_name=True)})

        request = b""
        replies = [r.ok.payload.to_bytes() for r in session.get(
            f"active/{args.sn}/config_file", payload=request, timeout=5) if r.ok]
        query_results["config_file"] = []
        qdir = args.output / "raw" / "queries" / "config_file"
        qdir.mkdir(parents=True)
        (qdir / "request_001.bin").write_bytes(request)
        for i, raw in enumerate(replies, 1):
            (qdir / f"response_{i:03d}.bin").write_bytes(raw)
            arr = pb.FileMsgArray.FromString(raw)
            decoded = [{"path": m.path, "seq": m.header.seq, "stamp": m.header.stamp,
                        "scaler": m.header.scaler, "bytes": len(m.data),
                        "sha256": digest(bytes(m.data))} for m in arr.array]
            query_results["config_file"].append({"file": f"raw/queries/config_file/response_{i:03d}.bin",
                                                  "bytes": len(raw), "sha256": digest(raw),
                                                  "decoded": decoded})

    stop.set()
    worker.join()
    ended_us = time.time_ns() // 1000
    with (args.output / "dual_stream_600s.csv").open("w", newline="", encoding="utf-8") as handle:
        out = csv.DictWriter(handle, fieldnames=["receive_unix_us", "stream", "seq", "stamp", "scaler"])
        out.writeheader()
        out.writerows(timing_rows)
    manifest = {"label": "rk3562-live-acceptance", "sn": args.sn, "endpoint": args.endpoint,
                "started_unix_us": started_us, "ended_unix_us": ended_us,
                "capture_seconds": args.seconds, "raw_packet_limit": args.raw_packets,
                "dropped": dropped, "packet_counts": packet_counts,
                "item_counts": item_counts, "query_results": query_results,
                "records": records}
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: manifest[k] for k in ("started_unix_us", "ended_unix_us", "dropped",
                                               "packet_counts", "item_counts", "query_results")},
                     ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
