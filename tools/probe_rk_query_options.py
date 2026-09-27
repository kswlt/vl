# -*- coding: utf-8 -*-
"""Try RK cmd queryable with explicit query target/consolidation options (zenoh 1.10)."""
import json
import time
import zenoh

EP = "tcp/203.0.113.179:7447#bind=203.0.113.200:0"
cfg = zenoh.Config()
cfg.insert_json5("mode", '"client"')
cfg.insert_json5("connect/endpoints", json.dumps([EP]))
cfg.insert_json5("scouting/multicast/enabled", "false")

variants = []
try:
    from zenoh import QueryTarget
    variants += [
        ("target=ALL", dict(target=QueryTarget.ALL)),
        ("target=BEST_MATCHING", dict(target=QueryTarget.BEST_MATCHING)),
        ("target=ALL_COMPLETE", dict(target=QueryTarget.ALL_COMPLETE)),
    ]
except ImportError as e:
    print("no QueryTarget", e)
try:
    from zenoh import QueryConsolidation
    for nm in ("NONE", "LATEST", "MONOTONIC"):
        if hasattr(QueryConsolidation, nm):
            variants.append((f"consolidation={nm}", dict(consolidation=getattr(QueryConsolidation, nm))))
except ImportError as e:
    print("no QueryConsolidation", e)

with zenoh.open(cfg) as s:
    time.sleep(0.5)
    for name, kw in variants:
        t0 = time.time()
        try:
            replies = list(s.get("active/LEGACY00000001/cmd/setting", payload=b"", timeout=4, **kw))
            dt = time.time() - t0
            if replies and replies[0].ok:
                print(f"{name}: OK {len(replies[0].ok.payload.to_bytes())}B {dt:.2f}s", flush=True)
            elif replies:
                print(f"{name}: ERR {replies[0].err} {dt:.2f}s", flush=True)
            else:
                print(f"{name}: NOREPLY {dt:.2f}s", flush=True)
        except Exception as e:
            print(f"{name}: EXC {type(e).__name__} {e}", flush=True)
        time.sleep(0.8)
