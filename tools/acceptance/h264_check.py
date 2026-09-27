#!/usr/bin/env python3
"""Inspect captured ImageMsg H264 Access Units and emit per-frame analysis."""
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from pc import active_msgs_pb2 as pb


def nals(data: bytes) -> list[tuple[int, bytes]]:
    starts = []
    pos = 0
    while pos + 3 < len(data):
        if data[pos:pos + 4] == b"\0\0\0\1":
            starts.append((pos, 4))
            pos += 4
        elif data[pos:pos + 3] == b"\0\0\1":
            starts.append((pos, 3))
            pos += 3
        else:
            pos += 1
    result = []
    for i, (start, prefix) in enumerate(starts):
        end = starts[i + 1][0] if i + 1 < len(starts) else len(data)
        if end > start + prefix:
            result.append((prefix, data[start + prefix:end]))
    return result


def slice_header(nal: bytes) -> tuple[int, int]:
    # H.264 7.3.3: first_mb_in_slice, slice_type are unsigned Exp-Golomb.
    data = bytearray()
    zeros = 0
    for byte in nal[1:]:
        if zeros >= 2 and byte == 3:
            zeros = 0
            continue
        data.append(byte)
        zeros = zeros + 1 if byte == 0 else 0
    bit_offset = 0

    def ue() -> int:
        nonlocal bit_offset
        leading = 0
        while True:
            if bit_offset >= len(data) * 8:
                raise ValueError("truncated H264 slice header")
            bit = (data[bit_offset // 8] >> (7 - bit_offset % 8)) & 1
            bit_offset += 1
            if bit:
                break
            leading += 1
            if leading > 31:
                raise ValueError("invalid Exp-Golomb code")
        value = 1
        for _ in range(leading):
            if bit_offset >= len(data) * 8:
                raise ValueError("truncated H264 slice header")
            value = (value << 1) | ((data[bit_offset // 8] >> (7 - bit_offset % 8)) & 1)
            bit_offset += 1
        return value - 1

    return ue(), ue()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("capture", type=Path)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--summary", type=Path)
    args = ap.parse_args()
    records = []
    errors = []
    frame_index = 0
    last_idr_index = None
    last_idr_stamp = None
    idr_intervals = []
    for file in sorted((args.capture / "raw" / "raw" / "image").glob("*.bin")):
        for msg in pb.ImageMsgArray.FromString(file.read_bytes()).array:
            frame_index += 1
            data = bytes(msg.data)
            units = nals(data)
            types = [payload[0] & 31 for _, payload in units]
            slices = []
            try:
                slices = [slice_header(payload) for _, payload in units if (payload[0] & 31) in (1, 5)]
            except ValueError as exc:
                errors.append(f"frame {frame_index}: {exc}")
            idr = 5 in types
            sps = 7 in types
            pps = 8 in types
            b_frame = any(kind % 5 == 1 for _, kind in slices)
            picture_starts = sum(first_mb == 0 for first_mb, _ in slices)
            if not units or units[0][0] not in (3, 4) or units[0][1][0] & 128:
                errors.append(f"frame {frame_index}: missing Annex-B start or forbidden bit")
            if not slices or picture_starts != 1:
                errors.append(f"frame {frame_index}: expected one complete picture in AU, got {picture_starts}")
            if msg.format != 3 or msg.width != 1920 or msg.height != 1080:
                errors.append(f"frame {frame_index}: format/resolution mismatch")
            if b_frame:
                errors.append(f"frame {frame_index}: B slice")
            if idr:
                if not sps or not pps:
                    errors.append(f"frame {frame_index}: IDR without SPS/PPS")
                if last_idr_stamp is not None:
                    interval = msg.header.stamp - last_idr_stamp
                    idr_intervals.append(interval)
                    if interval > 1000000:
                        errors.append(f"frame {frame_index}: IDR interval {interval}us > 1s")
                last_idr_index = frame_index
                last_idr_stamp = msg.header.stamp
            records.append({
                "frame_index": frame_index, "seq": msg.header.seq, "stamp": msg.header.stamp,
                "payload_size": len(data), "format": msg.format, "width": msg.width,
                "height": msg.height, "au_type": "IDR" if idr else "non-IDR",
                "is_idr": int(idr), "has_sps": int(sps), "has_pps": int(pps),
                "is_b_frame": int(b_frame),
                "start_code": units[0][0] if units else 0,
                "nal_types": ",".join(str(t) for t in types),
                "gop_position": frame_index - last_idr_index if last_idr_index else "",
                "source_file": file.relative_to(args.capture).as_posix(),
            })
    if not any(row["is_idr"] for row in records):
        errors.append("no IDR frame")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=list(records[0]) if records else ["frame_index"])
            writer.writeheader()
            writer.writerows(records)
    result = {"status": "PASS" if not errors else "FAIL", "frames": len(records),
              "idr_count": sum(row["is_idr"] for row in records),
              "b_frame_count": sum(row["is_b_frame"] for row in records),
              "max_idr_interval_us": max(idr_intervals) if idr_intervals else None,
              "gop_lengths": sorted(set(b - a for a, b in zip(
                  [i for i, row in enumerate(records) if row["is_idr"]],
                  [i for i, row in enumerate(records) if row["is_idr"]][1:]))),
              "errors": errors[:30]}
    rendered = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.summary:
        args.summary.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
