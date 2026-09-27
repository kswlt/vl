# -*- coding: utf-8 -*-
"""Queryable reliability matrix: each key probed N times with fresh sessions."""
import json
import sys
import time
import zenoh

endpoint = sys.argv[1]
KEYS = ["LEGACY00000001/cmd/setting", "VENDOR00000001/config_file"]
ROUNDS = 3

for key in KEYS:
    for i in range(ROUNDS):
        cfg = zenoh.Config()
        cfg.insert_json5("mode", '"client"')
        cfg.insert_json5("connect/endpoints", json.dumps([endpoint]))
        cfg.insert_json5("scouting/multicast/enabled", "false")
        t0 = time.time()
        status, detail = "?", ""
        try:
            with zenoh.open(cfg) as s:
                time.sleep(0.5)
                replies = list(s.get(f"active/{key}", payload=b"", timeout=5))
                dt = time.time() - t0
                if not replies:
                    status, detail = "NOREPLY", f"{dt:.2f}s"
                elif replies[0].ok:
                    status, detail = "OK", f"{len(replies[0].ok.payload.to_bytes())}B {dt:.2f}s"
                else:
                    status, detail = "TIMEOUT", f"{dt:.2f}s"
        except Exception as e:
            status, detail = "EXC", f"{type(e).__name__}:{e}"
        print(f"zenoh={zenoh.__version__ if hasattr(zenoh,'__version__') else '?'} key={key} round={i+1} -> {status} {detail}", flush=True)
        time.sleep(1.0)
