#!/usr/bin/env python3
"""Verify legacy-style H264 GOP and SPS/PPS placement in captured ImageMsg files."""

import argparse
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from pc import active_msgs_pb2 as pb


def nal_types(data: bytes) -> list[int]:
    starts: list[tuple[int, int]] = []
    i = 0
    while i + 3 < len(data):
        if data[i : i + 4] == b"\x00\x00\x00\x01":
            starts.append((i, 4))
            i += 4
        elif data[i : i + 3] == b"\x00\x00\x01":
            starts.append((i, 3))
            i += 3
        else:
            i += 1
    return [data[offset + prefix] & 0x1F for offset, prefix in starts if offset + prefix < len(data)]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument("--gop", type=int, default=3)
    args = parser.parse_args()

    with (args.capture / "metadata.csv").open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    frames = []
    for row in rows:
        message = pb.ImageMsg.FromString((args.capture / row["filename"]).read_bytes())
        types = nal_types(bytes(message.data))
        frames.append({"seq": message.header.seq, "types": types, "idr": 5 in types})

    idr_indexes = [index for index, frame in enumerate(frames) if frame["idr"]]
    distances = [right - left for left, right in zip(idr_indexes, idr_indexes[1:])]
    first_idr = idr_indexes[0] if idr_indexes else None
    aligned = first_idr is not None and all(
        frame["idr"] == ((index - first_idr) % args.gop == 0)
        for index, frame in enumerate(frames[first_idr:], start=first_idr)
    )
    parameter_sets_on_idr = all(7 in frames[index]["types"] and 8 in frames[index]["types"] for index in idr_indexes)
    result = {
        "frames": len(frames),
        "first_seq": frames[0]["seq"] if frames else None,
        "last_seq": frames[-1]["seq"] if frames else None,
        "idr_count": len(idr_indexes),
        "idr_distances": sorted(set(distances)),
        "gop": args.gop,
        "gop_aligned": aligned,
        "sps_pps_on_every_idr": parameter_sets_on_idr,
    }
    print(json.dumps(result, indent=2))
    if not aligned or set(distances) not in ({args.gop}, set()) or not parameter_sets_on_idr:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
