# -*- coding: utf-8 -*-
"""legacy control: repeat cmd/setting 6x with zenoh 1.10 client as stability control."""
import json
import time
import zenoh

EP = "tcp/192.0.2.55:7447#bind=192.0.2.100:0"
for i in range(1, 7):
    cfg = zenoh.Config()
    cfg.insert_json5("mode", '"client"')
    cfg.insert_json5("connect/endpoints", json.dumps([EP]))
    cfg.insert_json5("scouting/multicast/enabled", "false")
    t0 = time.time()
    with zenoh.open(cfg) as s:
        time.sleep(0.3)
        replies = list(s.get("active/LEGACY00000001/cmd/setting", payload=b"", timeout=5))
    dt = time.time() - t0
    if replies and replies[0].ok:
        print(f"LEGACY 1.10 setting #{i}: OK {len(replies[0].ok.payload.to_bytes())}B {dt:.2f}s", flush=True)
    else:
        print(f"LEGACY 1.10 setting #{i}: FAIL {replies} {dt:.2f}s", flush=True)
    time.sleep(0.8)
