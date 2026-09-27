#!/usr/bin/env python3
"""Exercise read-only Zenoh queries on a live Active-protocol device."""
import argparse
import json
import statistics
import sys
import time
from pathlib import Path

import zenoh

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pc import active_msgs_pb2 as pb


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--sn", required=True)
    parser.add_argument("--count", type=int, default=20)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    config = zenoh.Config()
    config.insert_json5("mode", '"client"')
    config.insert_json5("connect/endpoints", json.dumps([args.endpoint]))
    config.insert_json5("scouting/multicast/enabled", "false")
    results = {"endpoint": args.endpoint, "sn": args.sn, "queries": {}}
    with zenoh.open(config) as session:
        for name, payload in (
            ("setting", pb.SettingRequest(header=pb.Header(seq=0)).SerializeToString()),
            ("config_file", b""),
        ):
            attempts = []
            for _ in range(args.count):
                start = time.monotonic()
                try:
                    replies = list(session.get(
                        f"active/{args.sn}/{'cmd/' if name == 'setting' else ''}{name}",
                        payload=payload, timeout=3,
                    ))
                    good = [reply.ok.payload.to_bytes() for reply in replies if reply.ok]
                    if len(good) != 1:
                        raise RuntimeError(f"expected 1 reply, got {len(good)}")
                    if name == "setting":
                        decoded = pb.SettingResponse.FromString(good[0])
                        assert decoded.image_fps > 0 and decoded.lidar_fps > 0
                    else:
                        decoded = pb.FileMsgArray.FromString(good[0])
                        assert len(decoded.array) > 0
                    attempts.append({"ok": True, "ms": round((time.monotonic()-start)*1000, 2), "bytes": len(good[0])})
                except Exception as exc:
                    attempts.append({"ok": False, "ms": round((time.monotonic()-start)*1000, 2),
                                     "error": f"{type(exc).__name__}: {exc}"})
            success = [item["ms"] for item in attempts if item["ok"]]
            results["queries"][name] = {"success": len(success), "attempts": attempts,
                                         "median_ms": statistics.median(success) if success else None}
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as stream:
        json.dump(results, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print(json.dumps({key: {"success": value["success"], "total": len(value["attempts"]),
                             "median_ms": value["median_ms"]}
                      for key, value in results["queries"].items()}, ensure_ascii=False))
    return 0 if all(value["success"] == len(value["attempts"])
                    for value in results["queries"].values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
