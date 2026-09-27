# -*- coding: utf-8 -*-
"""Minimal setting query without protobuf dependency (empty SettingRequest = seq 0)."""
import json
import sys
import time

import zenoh

endpoint = sys.argv[1]
sn = sys.argv[2] if len(sys.argv) > 2 else "LEGACY00000001"
cfg = zenoh.Config()
cfg.insert_json5("mode", '"client"')
cfg.insert_json5("connect/endpoints", json.dumps([endpoint]))
cfg.insert_json5("scouting/multicast/enabled", "false")

with zenoh.open(cfg) as s:
    t0 = time.time()
    replies = list(s.get(f"active/{sn}/cmd/setting", payload=b"", timeout=5))
    dt = time.time() - t0
    print(f"replies={len(replies)} in {dt:.2f}s")
    for r in replies:
        if r.ok:
            raw = r.ok.payload.to_bytes()
            print(f"OK {len(raw)} bytes hex={raw.hex()}")
        else:
            print(f"ERR {r.err}")
