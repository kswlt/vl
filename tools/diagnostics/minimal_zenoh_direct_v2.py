#!/usr/bin/env python3
"""Zenoh direct probe using Config.from_json5 instead of incremental inserts."""

import argparse
import importlib.metadata
import json
import os
import sys
import time

os.environ.setdefault("RUST_LOG", "zenoh=debug,zenoh_transport=trace")

import zenoh


def emit(event, **fields):
    print(json.dumps({"event": event, "monotonic_us": time.monotonic_ns() // 1000,
                      **fields}, ensure_ascii=True), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", default="tcp/203.0.113.179:7447")
    parser.add_argument("--seconds", type=float, default=1.0)
    args = parser.parse_args()

    config_json5 = json.dumps({
        "mode": "client",
        "connect": {"endpoints": [args.endpoint], "timeout_ms": 10000,
                    "exit_on_failure": True},
        "scouting": {"multicast": {"enabled": False}},
    })
    config = zenoh.Config.from_json5(config_json5)
    emit("environment", executable=sys.executable, python=sys.version,
         zenoh_module=getattr(zenoh, "__file__", None),
         zenoh_version=importlib.metadata.version("eclipse-zenoh"),
         config_json5=config_json5, runtime_mode=config.get_json("mode"),
         runtime_endpoints=config.get_json("connect/endpoints"),
         runtime_multicast=config.get_json("scouting/multicast/enabled"))

    started = time.monotonic()
    try:
        with zenoh.open(config):
            emit("open_ok", open_ms=round((time.monotonic() - started) * 1000, 2))
            time.sleep(args.seconds)
    except Exception as exc:
        emit("open_error", elapsed_ms=round((time.monotonic() - started) * 1000, 2),
             exception_type=type(exc).__name__, exception=str(exc))
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
