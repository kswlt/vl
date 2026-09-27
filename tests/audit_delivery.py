"""Final local artifact, original HTTP payload, and raw recorder checks."""
import asyncio
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import time
from urllib.parse import unquote

import aiohttp
from PIL import Image
from pc import active_msgs_pb2 as pb
from tests.audit_live import Stats

ROOT = Path(__file__).resolve().parents[1]


async def main(output):
    report = {}
    missing = []
    for path in [ROOT/"README.md", ROOT/"HANDOFF.md", ROOT/"CODEX_AUDIT.md", *sorted((ROOT/"docs").glob("*.md"))]:
        for link in re.findall(r"\]\(([^)]+)\)", path.read_text(encoding="utf-8")):
            if "://" in link or link.startswith("#"):
                continue
            target = path.parent / unquote(link.split("#")[0])
            if not target.exists():
                missing.append(f"{path.name}: {link}")
    report["broken_document_links"] = missing
    stats = Stats()
    frame_id = ""
    hash_errors = 0
    async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=3)) as http:
        for _ in range(100):
            async with http.get("http://127.0.0.1:8080/api/image.pb", params={"after":frame_id}) as r:
                r.raise_for_status()
                if r.status != 200:
                    raise AssertionError(f"No new HTTP frame: {r.status}")
                payload = await r.read()
                stats.times.append(time.monotonic())
                stats.sizes.append(len(payload))
                arr = pb.ImageMsgArray.FromString(payload)
                assert len(arr.array) == 1
                msg = arr.array[0]
                assert msg.format == 2
                stats.header(msg.header)
                stats.jpeg(msg.data, msg.width, msg.height)
                frame_id = r.headers["X-Frame-Id"]
                hash_errors += hashlib.sha256(msg.data).hexdigest() != r.headers["X-Jpeg-Sha256"]
        async with http.get("http://127.0.0.1:8080/api/status") as r:
            report["runtime_status"] = await r.json()
    report["http_original_image_array_100"] = stats.report()
    report["http_jpeg_header_hash_errors"] = hash_errors
    count = 0
    path = sorted((ROOT/"raw_data"/"pointcloud").glob("pointcloud-*.bin"))[-1]
    with path.open("rb") as file:
        while count < 20:
            header = file.read(12)
            if len(header) != 12:
                break
            stamp, length = struct.unpack("<QI",header)
            payload = file.read(length)
            if len(payload) != length:
                break
            arr = pb.LidarPointMsgArray.FromString(payload)
            assert len(arr.array) == 1 and arr.array[0].header.stamp == stamp
            assert arr.array[0].header.scaler == 1000 and len(arr.array[0].points) > 0
            count += 1
    report["raw_complete_records_checked"] = count
    report["raw_file"] = path.relative_to(ROOT).as_posix()
    assert not missing and not hash_errors and count > 0 and not any(stats.errors.values())
    report["result"] = "PASS"
    Path(output).write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps({k:v for k,v in report.items() if k != "runtime_status"},indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default=str(ROOT/"evidence"/"delivery-final.json"))
    asyncio.run(main(parser.parse_args().output))
