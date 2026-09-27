#!/usr/bin/env python3
"""Minimal direct Zenoh client probe.  It does not alter gateway behaviour."""

import argparse
import importlib.metadata
import json
import os
import sys
import time
from collections import Counter

os.environ.setdefault("RUST_LOG", "zenoh=debug,zenoh_transport=debug")

import zenoh


def emit(event, **fields):
    print(json.dumps({"event": event, "monotonic_us": time.monotonic_ns() // 1000,
                      **fields}, ensure_ascii=True), flush=True)


def build_config(endpoint):
    config = zenoh.Config()
    config.insert_json5("mode", json.dumps("client"))
    config.insert_json5("connect/endpoints", json.dumps([endpoint]))
    config.insert_json5("connect/timeout_ms", "10000")
    config.insert_json5("connect/exit_on_failure", "true")
    config.insert_json5("scouting/multicast/enabled", "false")
    return config


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", default="tcp/203.0.113.179:7447")
    parser.add_argument("--sn", default="VENDOR00000001")
    parser.add_argument("--seconds", type=float, default=30.0)
    args = parser.parse_args()

    config = build_config(args.endpoint)
    emit("environment", executable=sys.executable, python=sys.version,
         zenoh_module=getattr(zenoh, "__file__", None),
         zenoh_version=importlib.metadata.version("eclipse-zenoh"),
         endpoint=args.endpoint, seconds=args.seconds,
         config_repr=repr(config))

    received = Counter()

    def callback(kind):
        def on_sample(sample):
            received[kind] += 1
            if received[kind] == 1:
                emit("first_sample", kind=kind, payload_bytes=len(sample.payload.to_bytes()))
        return on_sample

    started = time.monotonic()
    try:
        with zenoh.open(config) as session:
            emit("open_ok", open_ms=round((time.monotonic() - started) * 1000, 2))
            image_key = f"active/{args.sn}/image"
            cloud_key = f"active/{args.sn}/pointcloud"
            with session.declare_subscriber(image_key, callback("image")), \
                    session.declare_subscriber(cloud_key, callback("pointcloud")):
                deadline = time.monotonic() + args.seconds
                while time.monotonic() < deadline:
                    time.sleep(0.1)
            emit("complete", image=received["image"], pointcloud=received["pointcloud"])
    except Exception as exc:
        emit("open_or_subscribe_error", elapsed_ms=round((time.monotonic() - started) * 1000, 2),
             exception_type=type(exc).__name__, exception=str(exc))
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
