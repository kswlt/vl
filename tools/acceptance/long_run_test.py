#!/usr/bin/env python3
"""Run a sustained single-session capture, then check timing, rate and H264 sample."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "acceptance"


def run(*args: str) -> int:
    print("$", sys.executable, *args, flush=True)
    return subprocess.run([sys.executable, *args], check=False).returncode


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--endpoint", required=True)
    ap.add_argument("--sn", required=True)
    ap.add_argument("--seconds", type=float, default=7200)
    ap.add_argument("--raw-packets", type=int, default=2000)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error(f"output already exists: {args.output}")
    capture = ROOT / "tools" / "live_acceptance_capture.py"
    if run(str(capture), "--endpoint", args.endpoint, "--sn", args.sn,
           "--seconds", str(args.seconds), "--raw-packets", str(args.raw_packets),
           "--output", str(args.output)):
        return 1
    checks = [
        ("timestamp_check.py", [str(args.output), "--output", str(args.output / "timestamp_stats.json"),
                                "--min-seconds", str(min(60, args.seconds * 0.9))]),
        ("fps_check.py", [str(args.output), "--output", str(args.output / "fps_stats.json")]),
        ("h264_check.py", [str(args.output), "--output", str(args.output / "h264_analysis.csv"),
                           "--summary", str(args.output / "h264_summary.json")]),
        ("protobuf_wire_check.py", ["--capture", str(args.output)]),
    ]
    codes = {name: run(str(TOOLS / name), *arguments) for name, arguments in checks}
    manifest = json.loads((args.output / "manifest.json").read_text(encoding="utf-8"))
    if manifest["dropped"]:
        codes["callback_dropped"] = manifest["dropped"]
    result = {"status": "PASS" if all(code == 0 for code in codes.values()) else "FAIL",
              "capture_seconds": args.seconds, "packet_counts": manifest["packet_counts"],
              "item_counts": manifest["item_counts"], "check_exit_codes": codes}
    (args.output / "long_run_summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
