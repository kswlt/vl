# -*- coding: utf-8 -*-
"""Probe RK cmd queryables: setting vs unknown cmd, repeated, timing each reply."""
import json
import sys
import time

sys.path.insert(0, r"C:\Users\user\Desktop\project")
import zenoh
from pc import active_msgs_pb2 as pb

ENDPOINT = "tcp/203.0.113.179:7447#bind=203.0.113.200:0"
SN = "LEGACY00000001"

cfg = zenoh.Config()
cfg.insert_json5("mode", '"client"')
cfg.insert_json5("connect/endpoints", json.dumps([ENDPOINT]))
cfg.insert_json5("scouting/multicast/enabled", "false")

req = pb.SettingRequest(header=pb.Header(seq=0)).SerializeToString()

with zenoh.open(cfg) as s:
    for cmd, payload in [("setting", req), ("setting", req), ("ping_unknown", b""), ("setting", req)]:
        t0 = time.time()
        try:
            replies = list(s.get(f"active/{SN}/cmd/{cmd}", payload=payload, timeout=5))
            dt = time.time() - t0
            for r in replies:
                if r.ok:
                    raw = r.ok.payload.to_bytes()
                    try:
                        resp = pb.SettingResponse.FromString(raw)
                        print(f"{cmd}: OK in {dt:.2f}s code={resp.error.code} desc={resp.error.desc!r} "
                              f"imu={resp.imu_fps} lidar={resp.lidar_fps} img={resp.image_fps} fmt={resp.image_format} "
                              f"gop={resp.h264_gop} br={resp.h264_bitrate} parameter={resp.parameter[:120]!r}", flush=True)
                    except Exception as e:
                        print(f"{cmd}: OK in {dt:.2f}s raw={raw[:80]!r} parse_err={e}", flush=True)
                else:
                    print(f"{cmd}: REPLY-ERROR in {dt:.2f}s -> {r.err}", flush=True)
            if not replies:
                print(f"{cmd}: NO REPLY in {dt:.2f}s", flush=True)
        except Exception as e:
            print(f"{cmd}: EXCEPTION in {time.time()-t0:.2f}s -> {type(e).__name__}: {e}", flush=True)
