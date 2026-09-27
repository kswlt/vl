# -*- coding: utf-8 -*-
"""Probe a legacy-compatible device Zenoh endpoint: subscribe to active/** and report any traffic."""
import json
import sys
import time

import zenoh

ENDPOINT = sys.argv[1] if len(sys.argv) > 1 else "tcp/192.0.2.55:7447#bind=192.0.2.100:0"
DURATION = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0

cfg = zenoh.Config()
cfg.insert_json5("connect/endpoints", json.dumps([ENDPOINT]))
cfg.insert_json5("connect/timeout_ms", "0")
cfg.insert_json5("connect/exit_on_failure", "false")

print(f"opening session: {ENDPOINT}", flush=True)
session = zenoh.open(cfg)
time.sleep(1.0)
print(f"session closed? {session.is_closed()}", flush=True)

counts = {}
samples = []


def on_sample(sample):
    key = str(sample.kind) if hasattr(sample, "kind") else None
    expr = sample.key_expr
    payload = bytes(sample.payload)
    name = str(expr)
    counts[name] = counts.get(name, 0) + 1
    if len(samples) < 5:
        samples.append((name, len(payload)))
    print(f"  MSG {name}: {len(payload)} bytes", flush=True)


for keyexpr in ("active/**", "**"):
    try:
        sub = session.declare_subscriber(keyexpr, on_sample)
        print(f"subscribed {keyexpr}", flush=True)
    except Exception as exc:
        print(f"subscribe {keyexpr} failed: {exc}", flush=True)

t0 = time.time()
while time.time() - t0 < DURATION:
    time.sleep(0.5)

print("-" * 50, flush=True)
print(f"totals over {DURATION:.0f}s: {counts}", flush=True)
print(f"first samples: {samples}", flush=True)
session.close()
print("done", flush=True)
