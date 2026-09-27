#!/usr/bin/env python3
"""Save exact H264 ImageMsgArray payloads from the production image topic."""
import argparse
import csv
import hashlib
import json
import queue
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import zenoh
from pc import active_msgs_pb2 as pb

SN = "VENDOR00000001"
ENDPOINT = "tcp/203.0.113.179:7447#bind=203.0.113.200:0"

def main():
    p = argparse.ArgumentParser(); p.add_argument("--output", required=True); p.add_argument("--frames", type=int, default=100); p.add_argument("--endpoint", default=ENDPOINT); p.add_argument("--sn", default=SN); args = p.parse_args()
    out = Path(args.output); out.mkdir(parents=True, exist_ok=True)
    rows, received = [], queue.Queue(maxsize=256)
    cfg = zenoh.Config(); cfg.insert_json5("mode", '"client"'); cfg.insert_json5("connect/endpoints", json.dumps([args.endpoint])); cfg.insert_json5("scouting/multicast/enabled", "false")
    def callback(sample):
        try: received.put_nowait(sample.payload.to_bytes())
        except queue.Full: pass
    stream = bytearray()
    with zenoh.open(cfg) as session:
        sub = session.declare_subscriber(f"active/{args.sn}/image", callback)
        deadline = time.monotonic() + max(30, args.frames / 5 + 20)
        while len(rows) < args.frames and time.monotonic() < deadline:
            try: raw = received.get(timeout=.5)
            except queue.Empty: continue
            arr = pb.ImageMsgArray.FromString(raw)
            for msg in arr.array:
                if msg.format != pb.ImageFormatH264:
                    continue
                name = f"{len(rows)+1:06d}.bin"
                (out / name).write_bytes(msg.SerializeToString())
                stream.extend(msg.data)
                rows.append({"filename": name, "seq": msg.header.seq, "stamp_us": msg.header.stamp, "size": len(msg.data), "sha256": hashlib.sha256(msg.data).hexdigest(), "format": msg.format, "width": msg.width, "height": msg.height})
                if len(rows) >= args.frames:
                    break
        sub.undeclare()
    with (out / "metadata.csv").open("w", newline="", encoding="utf-8") as handle:
        csv.DictWriter(handle, fieldnames=rows[0].keys() if rows else ["filename"]).writeheader()
        if rows: csv.DictWriter(handle, fieldnames=rows[0].keys()).writerows(rows)
    (out.parent / "sample.h264").write_bytes(stream)
    print(json.dumps({"frames": len(rows), "stream_bytes": len(stream), "output": str(out)}, indent=2))
    if len(rows) < args.frames: raise SystemExit(1)

if __name__ == "__main__": main()
