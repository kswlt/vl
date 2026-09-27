# -*- coding: utf-8 -*-
"""Generic zenoh get probe: python probe_zenoh_get.py <endpoint> <full_key_suffix> [payload_hex]"""
import json
import sys
import time
import zenoh

endpoint, keysuffix = sys.argv[1], sys.argv[2]
payload = bytes.fromhex(sys.argv[3]) if len(sys.argv) > 3 else b""
cfg = zenoh.Config()
cfg.insert_json5("mode", '"client"')
cfg.insert_json5("connect/endpoints", json.dumps([endpoint]))
cfg.insert_json5("scouting/multicast/enabled", "false")
with zenoh.open(cfg) as s:
    t0 = time.time()
    try:
        replies = list(s.get(f"active/{keysuffix}", payload=payload, timeout=5))
    except Exception as e:
        print(f"EXCEPTION {type(e).__name__}: {e}")
        sys.exit(0)
    dt = time.time() - t0
    print(f"key=active/{keysuffix} replies={len(replies)} in {dt:.2f}s")
    for r in replies:
        if r.ok:
            raw = r.ok.payload.to_bytes()
            print(f"OK {len(raw)} bytes head_hex={raw[:40].hex()}")
        else:
            print(f"ERR {r.err}")
