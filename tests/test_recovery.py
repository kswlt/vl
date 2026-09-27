"""Isolated real-Zenoh outage/recovery test; never connects to the hardware."""
import asyncio
import argparse
import io
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

import aiohttp
from PIL import Image
import zenoh
from pc import active_msgs_pb2 as pb

ROOT = Path(__file__).resolve().parents[1]


def unused_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


async def run(output):
    endpoint = f"tcp/127.0.0.1:{unused_port()}"
    cfg = dict(sn="recovery-test", image_endpoint=endpoint, pointcloud_endpoint=endpoint,
               bind="127.0.0.1", http_port=unused_port(), foxglove_port=unused_port(),
               stale_seconds=1, reconnect_seconds=2, record_pointcloud=False)
    zcfg = zenoh.Config()
    zcfg.insert_json5("listen/endpoints", json.dumps([endpoint]))
    zcfg.insert_json5("scouting/multicast/enabled", "false")
    data = io.BytesIO()
    Image.new("RGB", (64, 32), "green").save(data, format="JPEG")
    frame = pb.ImageMsgArray()
    msg = frame.array.add(header=pb.Header(seq=1, stamp=1), width=64, height=32,
                          format=2, data=data.getvalue())
    cloud = pb.LidarPointMsgArray()
    points = cloud.array.add(header=pb.Header(seq=1, stamp=1, scaler=1000))
    points.points.add(x=-100, y=200, z=300)
    session = zenoh.open(zcfg)
    report = dict(kind="isolated_synthetic_zenoh_outage_not_physical_hotplug")
    with tempfile.TemporaryDirectory() as tmp:
        config = Path(tmp) / "config.json"
        config.write_text(json.dumps(cfg), encoding="utf-8")
        proc = subprocess.Popen([sys.executable, "-m", "pc.gateway", "--config", str(config)],
                                cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=2)) as http:
                url = f"http://127.0.0.1:{cfg['http_port']}"
                async def status():
                    async with http.get(url+"/api/status") as response:
                        response.raise_for_status()
                        return await response.json()
                for _ in range(50):
                    try:
                        await status()
                        break
                    except (aiohttp.ClientError, asyncio.TimeoutError):
                        await asyncio.sleep(.1)
                async def publish_until_healthy(previous=0):
                    for _ in range(100):
                        msg.header.seq += 1
                        msg.header.stamp = time.monotonic_ns()
                        points.header.CopyFrom(msg.header)
                        points.header.scaler = 1000
                        session.put("active/recovery-test/image", frame.SerializeToString())
                        session.put("active/recovery-test/pointcloud", cloud.SerializeToString())
                        await asyncio.sleep(.1)
                        state = await status()
                        if all(state[k]["healthy"] and state[k]["forwarded"] > previous for k in ("image","pointcloud")):
                            return state
                    raise AssertionError("subscriptions did not recover")
                before = await publish_until_healthy()
                async with http.get(url+"/latest.jpg") as response:
                    assert await response.read() == data.getvalue(), "JPEG changed in HTTP path"
                report["before"] = before
                image_only_http_ms = []
                for _ in range(30):
                    msg.header.seq += 1
                    msg.header.stamp = time.monotonic_ns()
                    session.put("active/recovery-test/image", frame.SerializeToString())
                    await asyncio.sleep(.1)
                    started = time.monotonic()
                    state = await status()
                    image_only_http_ms.append((time.monotonic()-started)*1000)
                    assert state["image"]["healthy"], "silent radar stopped image forwarding"
                assert not state["pointcloud"]["healthy"]
                assert state["pointcloud"]["reconnects"] == before["pointcloud"]["reconnects"]
                assert max(image_only_http_ms) < 750
                report["silent_radar_live_image"] = dict(max_http_ms=max(image_only_http_ms), state=state)
                session.close()
                session = None
                await asyncio.sleep(4)
                stale = await status()
                assert not stale["image"]["healthy"] and not stale["pointcloud"]["healthy"]
                async with http.get(url+"/latest.jpg") as response:
                    assert response.status == 503, "stale JPEG was served as current"
                report["during_outage"] = stale
                session = zenoh.open(zcfg)
                t = time.monotonic()
                after = await publish_until_healthy(max(before[k]["forwarded"] for k in ("image","pointcloud")))
                report["recovery_seconds"] = time.monotonic()-t
                report["after"] = after
                report["result"] = "PASS"
        finally:
            if session is not None:
                session.close()
            proc.terminate()
            proc.wait(timeout=10)
    path = Path(output)
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="evidence/recovery-latest.json")
    asyncio.run(run(parser.parse_args().output))
