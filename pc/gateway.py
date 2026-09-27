"""Zenoh protocol input, native JPEG preview, and official Foxglove SDK output."""
import argparse
import asyncio
import hashlib
import io
import json
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import queue
import shutil
import struct
import subprocess
import threading
import time

from aiohttp import web
import foxglove
from foxglove import channels, messages as fg
from PIL import Image
import zenoh

from . import active_msgs_pb2 as pb
from .recorder import RawRecorder

ROOT = Path(__file__).resolve().parents[1]
LOG = logging.getLogger("pc-gateway")
POINT = struct.Struct("<dddIfIi")
NT = fg.PackedElementFieldNumericType
POINT_FIELDS = [
    fg.PackedElementField(name=name, offset=offset, type=kind)
    for name, offset, kind in [
        ("x", 0, NT.Float64), ("y", 8, NT.Float64), ("z", 16, NT.Float64),
        ("rgbi", 24, NT.Uint32), ("intensity", 28, NT.Float32),
        ("ring", 32, NT.Uint32), ("offset", 36, NT.Int32),
    ]
]


def zenoh_config(endpoint):
    cfg = zenoh.Config()
    cfg.insert_json5("mode", json.dumps("client"))
    cfg.insert_json5("connect/endpoints", json.dumps([endpoint]))
    cfg.insert_json5("connect/timeout_ms", "0")
    cfg.insert_json5("connect/exit_on_failure", "false")
    cfg.insert_json5("scouting/multicast/enabled", "false")
    return cfg


class H264PreviewDecoder:
    """Decode original H264 access units to JPEG only for local display."""
    def __init__(self, executable=None, cfg=None):
        self.executable = executable
        self.cfg = cfg or {}
        self.proc = None
        # H264 P-frames reference earlier access units.  Unlike JPEG previews,
        # dropping arbitrary input frames corrupts the decode chain until the next IDR.
        self.inputs = queue.Queue(maxsize=120)
        self.lock = threading.Lock()
        self.write_lock = threading.Lock()
        self.stop = threading.Event()
        self.latest_jpeg = None
        self.latest_id = 0
        self.error = None

    def start(self):
        if self.proc and self.proc.poll() is None:
            return
        executable = self.executable or shutil.which("ffmpeg")
        if not executable:
            raise RuntimeError("ffmpeg is required to preview H264")
        command = [executable, "-hide_banner", "-loglevel", "warning", "-probesize", "10M",
             "-analyzeduration", "1M", "-fflags", "nobuffer", "-flags", "low_delay"]
        command += ["-f", "h264", "-i", "pipe:0", "-an", "-c:v", "mjpeg", "-q:v", "4",
             "-flush_packets", "1", "-f", "image2pipe", "pipe:1"]
        self.proc = subprocess.Popen(
            command,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            # No reader consumes FFmpeg stderr on Windows. PIPE would fill
            # with decoder warnings and block the H264 preview process.
            stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, "CREATE_NO_WINDOW") else 0,
        )
        threading.Thread(target=self._write_frames, name="h264-preview-input", daemon=True).start()
        threading.Thread(target=self._read_frames, name="h264-preview-output", daemon=True).start()

    def submit(self, data):
        self.start()
        try:
            self.inputs.put_nowait(data)
        except queue.Full:
            self.inputs.get_nowait()
            self.inputs.put_nowait(data)

    def _write_frames(self):
        while not self.stop.is_set():
            try:
                data = self.inputs.get(timeout=.1)
            except queue.Empty:
                continue
            try:
                with self.write_lock:
                    proc = self.proc
                    if not proc or not proc.stdin:
                        return
                    # An IDR access unit can exceed the Windows anonymous-pipe buffer.
                    # Incremental writes keep FFmpeg consuming before the producer blocks.
                    for offset in range(0, len(data), 4096):
                        proc.stdin.write(data[offset:offset + 4096])
                        proc.stdin.flush()
            except (BrokenPipeError, OSError) as exc:
                self.error = str(exc)
                return

    def _read_frames(self):
        data = b""
        try:
            while self.proc and self.proc.stdout:
                # BufferedReader.read() may wait for its entire requested size on Windows pipes.
                # read1() releases each encoded JPEG as soon as FFmpeg writes it.
                chunk = self.proc.stdout.read1(65536)
                if not chunk:
                    return
                data += chunk
                while True:
                    start = data.find(b"\xff\xd8")
                    end = data.find(b"\xff\xd9", start + 2)
                    if start < 0:
                        data = data[-2:]
                        break
                    if end < 0:
                        data = data[start:]
                        break
                    jpeg, data = data[start:end + 2], data[end + 2:]
                    with self.lock:
                        self.latest_jpeg = jpeg
                        self.latest_id += 1
        except OSError as exc:
            self.error = str(exc)

    def latest(self, previous_id):
        with self.lock:
            if self.latest_id == previous_id:
                return previous_id, None
            return self.latest_id, self.latest_jpeg

    def close(self):
        self.stop.set()
        proc, self.proc = self.proc, None
        if not proc:
            return
        try:
            if proc.stdin:
                proc.stdin.close()
            proc.wait(timeout=3)
        except (OSError, subprocess.TimeoutExpired):
            proc.kill()


class Source:
    def __init__(self, kind, cfg, notify, recorder=None):
        self.kind, self.cfg, self.notify = kind, cfg, notify
        self.recorder = recorder
        self.topic = f"active/{cfg['sn']}/{kind}"
        self.endpoint = cfg[f"{kind}_endpoint"]
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.pending = queue.Queue(maxsize=4)
        self.display_pending = queue.Queue(maxsize=1)
        self.display_stop = threading.Event()
        self.latest = None
        self.times = []
        self.last_rx = time.monotonic()
        self.stats = dict(received=0, forwarded=0, invalid=0, queue_dropped=0,
                          seq_gaps=0, seq_resets=0, timestamp_nonmono=0,
                          reconnects=0, last_seq=None, source_stamp_ns=None,
                          last_error=None, last_processed_mono=None, stale_events=0,
                          process_max_ms=0, sdk_log_max_ms=0, queue_delay_max_ms=0,
                          display_sent=0, display_dropped=0, preview_restarts=0)
        self.h264_video = kind == "image" and cfg.get("foxglove_image_mode") == "h264_video"
        self.preview_decoder = H264PreviewDecoder(cfg.get("ffmpeg_path"), cfg) if kind == "image" and not self.h264_video else None
        self.preview_id = 0
        self.preview_updated_mono = time.monotonic()
        channel_type = (channels.CompressedVideoChannel if self.h264_video else
                        channels.CompressedImageChannel if kind == "image" else channels.PointCloudChannel)
        self.channel = channel_type(self.topic, metadata={
            "source": "Zenoh", "source_endpoint": self.endpoint,
            "source_type": "active_msgs.ImageMsgArray" if kind == "image" else "active_msgs.LidarPointMsgArray",
            "display_clock": "PC receive wall clock; original stamps retained in /api/status",
        })
        self.thread = threading.Thread(target=self.run, name=f"zenoh-{kind}", daemon=True)
        self.display_thread = threading.Thread(target=self.display_loop,
                                               name=f"foxglove-{kind}", daemon=True)
        self.display_thread.start()

    def receive(self, sample):
        item = (sample.payload.to_bytes(), time.monotonic(), time.time_ns())
        with self.lock:
            self.stats["received"] += 1
            self.last_rx = item[1]
        try:
            self.pending.put_nowait(item)
        except queue.Full:
            try:
                self.pending.get_nowait()
                with self.lock:
                    self.stats["queue_dropped"] += 1
            except queue.Empty:
                pass
            try:
                self.pending.put_nowait(item)
            except queue.Full:
                with self.lock:
                    self.stats["queue_dropped"] += 1

    def enqueue_display(self, display, wall_ns):
        """Never let a slow Foxglove client block the protocol receive path."""
        try:
            self.display_pending.put_nowait((display, wall_ns))
        except queue.Full:
            try:
                self.display_pending.get_nowait()
            except queue.Empty:
                return
            with self.lock:
                self.stats["display_dropped"] += 1
            try:
                self.display_pending.put_nowait((display, wall_ns))
            except queue.Full:
                with self.lock:
                    self.stats["display_dropped"] += 1

    def restart_preview_decoder(self):
        old = self.preview_decoder
        self.preview_decoder = H264PreviewDecoder(self.cfg.get("ffmpeg_path"))
        self.preview_id = 0
        self.preview_updated_mono = time.monotonic()
        if old:
            old.close()
        with self.lock:
            self.stats["preview_restarts"] += 1
        LOG.warning("restarting stalled H264 preview decoder")

    def display_loop(self):
        # Full-resolution JPEGs are intentionally capped for the display transport.
        # The source receives every H264 access unit and keeps the newest 4K preview.
        max_hz = float(self.cfg.get("foxglove_image_max_hz", 10)) if self.kind == "image" and not self.h264_video else 0
        min_interval, last_sent = (1.0 / max_hz if max_hz > 0 else 0), 0.0
        while not self.display_stop.is_set():
            try:
                display, wall_ns = self.display_pending.get(timeout=.1)
            except queue.Empty:
                continue
            remaining = min_interval - (time.monotonic() - last_sent)
            if remaining > 0:
                self.display_stop.wait(remaining)
            if self.display_stop.is_set():
                return
            try:
                self.channel.log(display, log_time=wall_ns)
                last_sent = time.monotonic()
                with self.lock:
                    self.stats["display_sent"] += 1
            except Exception as exc:
                with self.lock:
                    self.stats["last_error"] = f"Foxglove display: {exc}"
                LOG.warning("Foxglove %s display error: %s", self.kind, exc)

    def process(self, item):
        payload, received_mono, wall_ns = item
        started = time.monotonic()
        arr = pb.ImageMsgArray() if self.kind == "image" else pb.LidarPointMsgArray()
        arr.ParseFromString(payload)
        if not arr.array:
            raise ValueError("empty message array")
        messages = list(arr.array)
        if any(not item.HasField("header") for item in messages):
            raise ValueError("missing Header")
        msg = messages[-1]
        batch_size = len(messages)
        timestamp = fg.Timestamp(sec=wall_ns // 1_000_000_000, nsec=wall_ns % 1_000_000_000)
        extra = {}
        if self.kind == "image":
            formats = {item.format for item in messages}
            if len(formats) != 1:
                raise ValueError(f"mixed image formats in one array: {sorted(formats)}")
            if msg.format == pb.ImageFormatJpeg:
                jpeg = msg.data
                if jpeg[:2] != b"\xff\xd8" or jpeg[-2:] != b"\xff\xd9":
                    raise ValueError("not a complete protocol JPEG")
                with Image.open(io.BytesIO(jpeg)) as im:
                    if im.format != "JPEG" or im.size != (msg.width, msg.height):
                        raise ValueError("JPEG dimensions do not match ImageMsg")
                source_format = "JPEG"
            elif msg.format == pb.ImageFormatH264:
                if not msg.data.startswith((b"\x00\x00\x01", b"\x00\x00\x00\x01")):
                    raise ValueError("H264 access unit is missing an Annex-B start code")
                source_format = "H264"
                if self.h264_video:
                    jpeg = None
                    display = fg.CompressedVideo(timestamp=timestamp, frame_id="camera",
                                                 data=msg.data, format="h264")
                else:
                    for item in messages:
                        if not item.data.startswith((b"\x00\x00\x01", b"\x00\x00\x00\x01")):
                            raise ValueError("H264 access unit is missing an Annex-B start code")
                        self.preview_decoder.submit(item.data)
                    preview_id, jpeg = self.preview_decoder.latest(self.preview_id)
                    if preview_id == self.preview_id:
                        if time.monotonic() - self.preview_updated_mono > 2:
                            self.restart_preview_decoder()
                        return
                    self.preview_id = preview_id
                    self.preview_updated_mono = time.monotonic()
                    with Image.open(io.BytesIO(jpeg)) as im:
                        if im.format != "JPEG":
                            raise ValueError("H264 preview decoder returned non-JPEG output")
                        preview_width, preview_height = im.size
            else:
                raise ValueError(f"unsupported ImageMsg.format={msg.format}")
            if msg.format != pb.ImageFormatH264 or not self.h264_video:
                display = fg.CompressedImage(timestamp=timestamp, frame_id="camera", data=jpeg, format="jpeg")
            extra = dict(width=msg.width, height=msg.height, batch_size=batch_size,
                         jpeg_bytes=len(jpeg) if jpeg is not None else 0,
                         sha256=hashlib.sha256(jpeg if jpeg is not None else msg.data).hexdigest(), source_format=source_format,
                         source_bytes=len(msg.data))
            if source_format == "H264" and not self.h264_video:
                extra.update(preview_width=preview_width, preview_height=preview_height)
            base_width = preview_width if source_format == "H264" and not self.h264_video else msg.width
            base_height = preview_height if source_format == "H264" and not self.h264_video else msg.height
            ai_width = min(int(self.cfg.get("ai_preview_width", 1920)), base_width)
            if ai_width < base_width:
                with Image.open(io.BytesIO(jpeg)) as im:
                    ai_height = max(1, round(im.height * ai_width / im.width))
                    ai_image = im.resize((ai_width, ai_height), Image.Resampling.BILINEAR)
                    ai_buf = io.BytesIO()
                    ai_image.save(ai_buf, format="JPEG", quality=int(self.cfg.get("ai_preview_quality", 85)), optimize=False)
                    ai_data = ai_buf.getvalue()
            else:
                ai_width, ai_height, ai_data = base_width, base_height, jpeg
            extra.update(ai_preview_width=ai_width, ai_preview_height=ai_height,
                         ai_jpeg_bytes=len(ai_data) if ai_data else 0)
        else:
            if msg.header.scaler != 1000:
                raise ValueError(f"pointcloud scaler={msg.header.scaler}, expected 1000")
            data = bytearray(POINT.size * len(msg.points))
            for index, p in enumerate(msg.points):
                POINT.pack_into(data, index * POINT.size, p.x / 1000, p.y / 1000, p.z / 1000,
                                p.rgbi, float(p.rgbi & 255), p.ring, p.offset)
            display = fg.PointCloud(timestamp=timestamp, frame_id="lidar",
                                    pose=fg.Pose(orientation=fg.Quaternion(w=1)),
                                    point_stride=POINT.size, fields=POINT_FIELDS, data=bytes(data))
            point_data = bytes(data)
            extra = dict(points=len(msg.points), displayed_points=len(data) // POINT.size,
                         batch_size=batch_size)
            if self.recorder:
                self.recorder.submit(msg.header.stamp, payload)
        # Foxglove output is deliberately decoupled: a slow UI only drops display frames.
        self.enqueue_display(display, wall_ns)
        finished = time.monotonic()
        with self.lock:
            self.stats["process_max_ms"] = max(self.stats["process_max_ms"], (finished-started)*1000)
            self.stats["queue_delay_max_ms"] = max(self.stats["queue_delay_max_ms"], (started-received_mono)*1000)
            prev, stamp = self.stats["last_seq"], self.stats["source_stamp_ns"]
            if prev is not None:
                delta = (messages[0].header.seq - prev) & 0xffffffff
                if 1 < delta < 0x80000000:
                    self.stats["seq_gaps"] += delta - 1
                elif delta == 0 or delta >= 0x80000000:
                    self.stats["seq_resets"] += 1
                if msg.header.stamp <= stamp:
                    self.stats["timestamp_nonmono"] += 1
            for left, right in zip(messages, messages[1:]):
                delta = (right.header.seq - left.header.seq) & 0xffffffff
                if 1 < delta < 0x80000000:
                    self.stats["seq_gaps"] += delta - 1
                elif delta == 0 or delta >= 0x80000000:
                    self.stats["seq_resets"] += 1
            self.stats.update(last_seq=msg.header.seq, source_stamp_ns=msg.header.stamp,
                              last_processed_mono=received_mono, last_error=None, **extra)
            self.stats["forwarded"] += batch_size
            self.times.extend([received_mono] * batch_size)
            self.times = [t for t in self.times if t >= received_mono - 5]
            self.latest = dict(payload=payload, data=jpeg if self.kind == "image" else None,
                               point_data=point_data if self.kind == "pointcloud" else None,
                               ai_data=ai_data if self.kind == "image" else None,
                               frame_id=self.stats["forwarded"], seq=msg.header.seq,
                               received_mono=received_mono, wall_ns=wall_ns, **extra)
        self.notify()

    def status(self):
        with self.lock:
            stats = dict(self.stats)
            now = time.monotonic()
            age = None if self.latest is None else now - self.latest["received_mono"]
            times = [t for t in self.times if t >= now - 5]
            hz = (len(times) - 1) / (times[-1] - times[0]) if len(times) > 1 else 0.0
        stats.update(topic=self.topic, endpoint=self.endpoint, age_seconds=age,
                     hz=hz, healthy=age is not None and age < self.cfg["stale_seconds"])
        return stats

    def run(self):
        while not self.stop.is_set():
            session = None
            try:
                session = zenoh.open(zenoh_config(self.endpoint))
                subscriber = session.declare_subscriber(self.topic, self.receive)
                with self.lock:
                    self.last_rx = time.monotonic()
                LOG.info("subscribed %s at %s", self.topic, self.endpoint)
                stale_reported = False
                while not self.stop.is_set():
                    try:
                        item = self.pending.get(timeout=0.25)
                    except queue.Empty:
                        if session.is_closed():
                            raise ConnectionError("Zenoh session closed")
                        with self.lock:
                            age = time.monotonic() - self.last_rx
                        # A silent sensor is not a broken subscription. Zenoh restores its TCP links.
                        if age > self.cfg["reconnect_seconds"] and not stale_reported:
                            stale_reported = True
                            with self.lock:
                                self.stats["stale_events"] += 1
                                self.stats["last_error"] = f"no sample for {age:.1f}s; subscription retained"
                            LOG.warning("%s source stale; retaining Zenoh subscription", self.kind)
                        continue
                    try:
                        self.process(item)
                        stale_reported = False
                    except Exception as exc:
                        with self.lock:
                            self.stats["invalid"] += 1
                            self.stats["last_error"] = str(exc)
                        LOG.exception("invalid %s sample", self.kind)
                subscriber.undeclare()
            except Exception as exc:
                with self.lock:
                    self.stats["reconnects"] += 1
                    self.stats["last_error"] = str(exc)
                LOG.warning("%s reconnect: %s", self.kind, exc)
            finally:
                if session is not None:
                    session.close()
            self.stop.wait(0.5)

    def close(self):
        self.display_stop.set()
        if self.preview_decoder:
            self.preview_decoder.close()


async def serve(cfg):
    loop = asyncio.get_running_loop()
    updated = asyncio.Event()
    diagnostics = dict(event_loop_lag_max_ms=0)
    async def watch_loop():
        while True:
            started = loop.time()
            await asyncio.sleep(.1)
            lag = max(0, (loop.time()-started-.1)*1000)
            diagnostics["event_loop_lag_max_ms"] = max(diagnostics["event_loop_lag_max_ms"], lag)
            if lag > 250:
                LOG.warning("HTTP event loop delayed %.1fms", lag)
    loop_watch = asyncio.create_task(watch_loop())
    instance_id = cfg.get("instance_id", "default")
    recorder = RawRecorder(ROOT / "raw_data" / f"pointcloud-{instance_id}") if cfg.get("record_pointcloud", True) else None
    if recorder:
        recorder.thread.start()
    sources = {kind: Source(kind, cfg, lambda: loop.call_soon_threadsafe(updated.set),
                            recorder if kind == "pointcloud" else None)
               for kind in ("image", "pointcloud")}
    server = foxglove.start_server(name=f"Gateway | {cfg.get('display_name', instance_id)}", host=cfg["bind"],
                                   port=cfg["foxglove_port"], message_backlog_size=4)
    for source in sources.values():
        source.thread.start()
    app = web.Application()

    async def index(request):
        return web.FileResponse(ROOT / "pc" / "viewer.html", headers={"Cache-Control": "no-store"})

    async def status(request):
        return web.json_response({"time_ns": time.time_ns(),
                                  "device": {"instance_id": instance_id,
                                             "display_name": cfg.get("display_name", instance_id),
                                             "device_ip": cfg.get("device_ip"),
                                             "device_api": cfg.get("device_api"),
                                             "sn": cfg["sn"]},
                                  "diagnostics": dict(diagnostics),
                                  "recorder": recorder.status() if recorder else None,
                                  **{name: source.status() for name, source in sources.items()}},
                                 headers={"Cache-Control": "no-store"})

    async def latest(request):
        after = request.query.get("after", "")
        deadline = loop.time() + 1.0
        while True:
            # Clear first so an update between inspection and wait is not lost.
            updated.clear()
            source = sources["image"]
            with source.lock:
                frame = source.latest
            if frame and str(frame["frame_id"]) != after:
                break
            remaining = deadline - loop.time()
            if remaining <= 0:
                return web.Response(status=204, headers={"Cache-Control": "no-store"})
            try:
                await asyncio.wait_for(updated.wait(), remaining)
            except asyncio.TimeoutError:
                return web.Response(status=204, headers={"Cache-Control": "no-store"})
        age = time.monotonic() - frame["received_mono"]
        if age > cfg["stale_seconds"]:
            return web.Response(status=503, text="Camera data stale", headers={"Cache-Control": "no-store"})
        headers = {"Cache-Control": "no-store", "X-Frame-Id": str(frame["frame_id"]),
                   "X-Source-Seq": str(frame["seq"]), "X-Frame-Age-Ms": f"{age * 1000:.1f}",
                   "X-Jpeg-Sha256": frame["sha256"]}
        if request.path == "/api/image.pb":
            return web.Response(body=frame["payload"], content_type="application/x-protobuf", headers=headers)
        if frame["data"] is None:
            return web.Response(status=409, text="H264 direct video is available in Foxglove", headers=headers)
        if request.path == "/latest.ai.jpg":
            return web.Response(body=frame["ai_data"], content_type="image/jpeg", headers=headers)
        if request.path == "/latest.bundle":
            bundle = struct.pack("<I", len(frame["ai_data"])) + frame["ai_data"] + frame["data"]
            return web.Response(body=bundle, content_type="application/octet-stream", headers=headers)
        return web.Response(body=frame["data"], content_type="image/jpeg", headers=headers)

    async def latest_pointcloud(_request):
        source = sources["pointcloud"]
        with source.lock:
            frame = source.latest
        if not frame or frame.get("point_data") is None:
            return web.Response(status=503, text="Point cloud unavailable",
                                headers={"Cache-Control": "no-store"})
        age = time.monotonic() - frame["received_mono"]
        if age > cfg["stale_seconds"]:
            return web.Response(status=503, text="Point cloud stale",
                                headers={"Cache-Control": "no-store"})
        return web.Response(body=frame["point_data"], content_type="application/octet-stream",
                            headers={"Cache-Control": "no-store", "X-Point-Stride": str(POINT.size),
                                     "X-Point-Count": str(frame["points"]),
                                     "X-Source-Seq": str(frame["seq"]),
                                     "X-Frame-Age-Ms": f"{age * 1000:.1f}"})

    async def device_status(_request):
        url = cfg.get("device_api")
        if not url:
            return web.json_response({"available": False, "error": "device_api not configured"}, status=503)
        try:
            import urllib.request
            def fetch():
                with urllib.request.urlopen(url, timeout=1.5) as response:
                    return json.loads(response.read().decode("utf-8"))
            payload = await asyncio.to_thread(fetch)
            return web.json_response({"available": True, **payload}, headers={"Cache-Control": "no-store"})
        except Exception as exc:
            return web.json_response({"available": False, "error": str(exc)}, status=503,
                                     headers={"Cache-Control": "no-store"})

    app.router.add_get("/", index)
    app.router.add_get("/api/status", status)
    app.router.add_get("/latest.jpg", latest)
    app.router.add_get("/latest.ai.jpg", latest)
    app.router.add_get("/latest.bundle", latest)
    app.router.add_get("/api/image.pb", latest)
    app.router.add_get("/api/pointcloud.bin", latest_pointcloud)
    app.router.add_get("/api/device", device_status)
    runner = web.AppRunner(app, access_log=None)
    await runner.setup()
    await web.TCPSite(runner, cfg["bind"], cfg["http_port"]).start()
    time_runner = None
    time_bind = cfg.get("board_time_sync_bind")
    time_port = cfg.get("board_time_sync_port")
    if time_bind and time_port:
        # Deliberately expose only an integer UTC epoch to the directly attached
        # board NIC. The full gateway UI/API remains loopback-only.
        time_app = web.Application()

        async def board_time(_request):
            return web.Response(text=f"{time.time_ns() // 1_000_000_000}\n",
                                content_type="text/plain",
                                headers={"Cache-Control": "no-store"})

        time_app.router.add_get("/time", board_time)
        time_runner = web.AppRunner(time_app, access_log=None)
        await time_runner.setup()
        await web.TCPSite(time_runner, time_bind, int(time_port)).start()
        LOG.info("board time source ready http://%s:%s/time", time_bind, time_port)
    LOG.info("ready HTTP=%s:%s Foxglove=%s:%s", cfg["bind"], cfg["http_port"], cfg["bind"], cfg["foxglove_port"])
    try:
        while True:
            await asyncio.sleep(10)
            LOG.info("health %s", json.dumps({kind: source.status() for kind, source in sources.items()}))
    finally:
        loop_watch.cancel()
        for source in sources.values():
            source.stop.set()
        for source in sources.values():
            await asyncio.to_thread(source.thread.join, 3)
            source.close()
        if recorder:
            recorder.stop.set()
            await asyncio.to_thread(recorder.thread.join, 3)
        if time_runner:
            await time_runner.cleanup()
        await runner.cleanup()
        server.stop()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, default=ROOT / "config" / "pc.json")
    parser.add_argument("--http-port", type=int)
    parser.add_argument("--foxglove-port", type=int)
    parser.add_argument("--console", action="store_true")
    args = parser.parse_args()
    cfg = json.loads(args.config.read_text(encoding="utf-8"))
    if args.http_port:
        cfg["http_port"] = args.http_port
    if args.foxglove_port:
        cfg["foxglove_port"] = args.foxglove_port
    (ROOT / "runtime").mkdir(exist_ok=True)
    instance_id = cfg.get("instance_id", "gateway")
    handlers = [RotatingFileHandler(ROOT / "runtime" / f"gateway-{instance_id}.log", maxBytes=4_000_000,
                                    backupCount=3, encoding="utf-8")]
    if args.console:
        handlers.append(logging.StreamHandler())
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s", handlers=handlers)
    try:
        asyncio.run(serve(cfg))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
