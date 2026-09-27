"""Record real RK stream samples around one requested software reboot."""
from __future__ import annotations

import argparse
import base64
import json
import socket
import sys
import time
import urllib.request
from pathlib import Path

import zenoh
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pc import active_msgs_pb2 as pb


def status(host: str) -> dict:
    return json.load(urllib.request.urlopen(f"http://{host}:8080/api/management/status", timeout=2))


def sample(host: str, sn: str, duration: float, output: Path, label: str) -> dict:
    c = zenoh.Config()
    c.insert_json5("mode", '"client"')
    c.insert_json5("connect/endpoints", json.dumps([f"tcp/{host}:7447"]))
    c.insert_json5("scouting/multicast/enabled", "false")
    result = {"image": [], "pointcloud": []}

    def receive(s):
        kind = str(s.key_expr).rsplit("/", 1)[-1]
        if kind not in result:
            return
        raw = bytes(s.payload)
        messages = (pb.ImageMsgArray if kind == "image" else pb.LidarPointMsgArray).FromString(raw).array
        for m in messages:
            item = {"received_us": time.time_ns() // 1000, "seq": m.header.seq, "stamp": m.header.stamp}
            if kind == "image":
                item.update(format=m.format, width=m.width, height=m.height)
            else:
                item.update(scaler=m.header.scaler, points=len(m.points))
            result[kind].append(item)
        if len(result[kind]) == len(messages):
            (output / f"{label}_{kind}.bin").write_bytes(raw)

    with zenoh.open(c) as session:
        subs = [session.declare_subscriber(f"active/{sn}/{kind}", receive) for kind in result]
        time.sleep(duration)
        for sub in subs:
            sub.undeclare()
    return {kind: {"count": len(items), "first": items[0] if items else None, "last": items[-1] if items else None} for kind, items in result.items()}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--sn", required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    report = {"host": args.host, "sn": args.sn}
    report["pre_status"] = status(args.host)
    report["pre_stream"] = sample(args.host, args.sn, 4, args.output, "pre")
    report["reboot_request_unix_us"] = time.time_ns() // 1000
    req = urllib.request.Request(f"http://{args.host}:8080/api/management/reboot", data=b"{}", method="POST", headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as response:
        report["reboot_response"] = {"status": response.status, "body": response.read().decode()}
    print("reboot accepted; waiting for cold service recovery", flush=True)
    deadline = time.monotonic() + 110
    while time.monotonic() < deadline:
        try:
            current = status(args.host)
            if current["system"]["uptime_s"] < report["pre_status"]["system"]["uptime_s"]:
                report["post_status"] = current
                report["http_recovered_unix_us"] = time.time_ns() // 1000
                break
        except Exception:
            pass
        time.sleep(1)
    if "post_status" not in report:
        report["error"] = "HTTP management did not recover within 110 seconds"
    else:
        report["post_stream"] = sample(args.host, args.sn, 10, args.output, "post")
        try:
            with socket.create_connection((args.host, 22), timeout=3) as s:
                s.settimeout(3)
                report["ssh_banner"] = s.recv(100).decode(errors="replace")
        except Exception as exc:
            report["ssh_error"] = repr(exc)
    (args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report.get(key) for key in ("reboot_response", "http_recovered_unix_us", "post_stream", "ssh_banner", "ssh_error", "error")}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
