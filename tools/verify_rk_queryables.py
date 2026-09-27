"""Stress RK queryables with the mainstream zenoh 1.10 client under live load."""
from __future__ import annotations

import json
import argparse
import sys
import time

import zenoh

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[1]))
from pc import active_msgs_pb2 as pb


ENDPOINT = "tcp/203.0.113.179:7447#bind=203.0.113.200:0"
SN = "DEVICE00000001"

parser = argparse.ArgumentParser()
parser.add_argument("--persistent-rounds", type=int, default=50)
parser.add_argument("--fresh-rounds", type=int, default=50)
parser.add_argument("--fresh-settle", type=float, default=0.0,
                    help="seconds to wait after each fresh session opens")
args = parser.parse_args()

config = zenoh.Config()
config.insert_json5("mode", '"client"')
config.insert_json5("connect/endpoints", json.dumps([ENDPOINT]))
config.insert_json5("scouting/multicast/enabled", "false")


def new_config():
    cfg = zenoh.Config()
    cfg.insert_json5("mode", '"client"')
    cfg.insert_json5("connect/endpoints", json.dumps([ENDPOINT]))
    cfg.insert_json5("scouting/multicast/enabled", "false")
    return cfg


def query(session, key: str, payload: bytes, response_type, rounds: int) -> dict:
    successes = 0
    errors: list[str] = []
    elapsed: list[float] = []
    sizes: list[int] = []
    for index in range(rounds):
        started = time.perf_counter()
        replies = list(session.get(f"active/{SN}/{key}", payload=payload, timeout=2))
        elapsed.append(time.perf_counter() - started)
        ok = [reply.ok for reply in replies if reply.ok]
        if len(ok) != 1:
            errors.append(f"round {index + 1}: replies={replies}")
            continue
        raw = ok[0].payload.to_bytes()
        response_type.FromString(raw)
        sizes.append(len(raw))
        successes += 1
    return {
        "key": key,
        "successes": successes,
        "rounds": rounds,
        "success_rate": successes / rounds,
        "max_seconds": max(elapsed),
        "mean_seconds": sum(elapsed) / len(elapsed),
        "payload_sizes": sorted(set(sizes)),
        "errors": errors,
    }


setting_request = pb.SettingRequest(header=pb.Header(seq=1)).SerializeToString()
bad_reboot = b"\x80"  # malformed protobuf: must reply with an error, never reboot

report = {"zenoh": "1.10 client environment", "endpoint": ENDPOINT, "sn": SN}
if args.persistent_rounds:
    with zenoh.open(config) as session:
        time.sleep(0.5)
        report.update({
            "setting": query(session, "cmd/setting", setting_request, pb.SettingResponse,
                             args.persistent_rounds),
            "malformed_reboot": query(session, "cmd/reboot", bad_reboot, pb.RebootResponse,
                                      max(1, args.persistent_rounds // 5)),
            "config_file": query(session, "config_file", b"", pb.FileMsgArray,
                                 max(1, args.persistent_rounds // 5)),
        })

fresh_successes = 0
fresh_times = []
fresh_errors = []
for index in range(args.fresh_rounds):
    started = time.perf_counter()
    try:
        with zenoh.open(new_config()) as session:
            if args.fresh_settle:
                time.sleep(args.fresh_settle)
            replies = list(session.get(
                f"active/{SN}/cmd/setting", payload=setting_request, timeout=2
            ))
            ok = [reply.ok for reply in replies if reply.ok]
            if len(ok) != 1:
                fresh_errors.append(f"round {index + 1}: replies={replies}")
            else:
                pb.SettingResponse.FromString(ok[0].payload.to_bytes())
                fresh_successes += 1
    except Exception as error:
        fresh_errors.append(f"round {index + 1}: {type(error).__name__}: {error}")
    fresh_times.append(time.perf_counter() - started)
report["setting_fresh_sessions"] = {
    "successes": fresh_successes,
    "rounds": args.fresh_rounds,
    "success_rate": fresh_successes / args.fresh_rounds if args.fresh_rounds else 1.0,
    "max_seconds": max(fresh_times) if fresh_times else 0.0,
    "mean_seconds": sum(fresh_times) / len(fresh_times) if fresh_times else 0.0,
    "errors": fresh_errors,
}

print(json.dumps(report, ensure_ascii=False, indent=2))
if any(value["success_rate"] != 1.0 for name, value in report.items()
       if name in ("setting", "malformed_reboot", "config_file", "setting_fresh_sessions")):
    raise SystemExit(1)
