"""Bounded asynchronous raw pointcloud recorder, separate from visualization I/O."""
from collections import deque
from pathlib import Path
import queue
import struct
import threading
import time


class RawRecorder:
    def __init__(self, directory, window_seconds=300, shard_seconds=60):
        self.directory = Path(directory)
        self.window = window_seconds
        self.shard_seconds = shard_seconds
        self.pending = queue.Queue(maxsize=64)
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.stats = dict(written=0, queue_dropped=0, write_errors=0, last_error=None)
        self.thread = threading.Thread(target=self.run, name="raw-pointcloud", daemon=True)

    def submit(self, stamp_ns, payload):
        try:
            self.pending.put_nowait((stamp_ns, payload))
        except queue.Full:
            with self.lock:
                self.stats["queue_dropped"] += 1

    def status(self):
        with self.lock:
            return dict(self.stats)

    def run(self):
        file = None
        shards = deque()
        opened = 0
        try:
            self.directory.mkdir(parents=True, exist_ok=True)
            # Only this recorder's files are eligible; never touch legacy raw_data files.
            for path in self.directory.glob("pointcloud-*.bin"):
                if path.stat().st_mtime < time.time() - self.window - self.shard_seconds:
                    path.unlink()
            while not self.stop.is_set() or not self.pending.empty():
                try:
                    stamp, payload = self.pending.get(timeout=.25)
                except queue.Empty:
                    continue
                try:
                    now = time.monotonic()
                    if file is None or now - opened >= self.shard_seconds:
                        if file is not None:
                            file.close()
                        path = self.directory / f"pointcloud-{time.time_ns()}.bin"
                        file = path.open("xb")
                        opened = now
                        shards.append((now, path))
                        while shards and now - shards[0][0] > self.window + self.shard_seconds:
                            shards.popleft()[1].unlink(missing_ok=True)
                        for old in self.directory.glob("pointcloud-*.bin"):
                            if old != path and old.stat().st_mtime < time.time() - self.window - self.shard_seconds:
                                old.unlink()
                    file.write(struct.pack("<QI", stamp, len(payload)))
                    file.write(payload)
                    file.flush()
                    with self.lock:
                        self.stats["written"] += 1
                        self.stats["last_error"] = None
                except OSError as exc:
                    with self.lock:
                        self.stats["write_errors"] += 1
                        self.stats["last_error"] = str(exc)
                    if file is not None:
                        try:
                            file.close()
                        except OSError:
                            pass
                        file = None
        except OSError as exc:
            with self.lock:
                self.stats["write_errors"] += 1
                self.stats["last_error"] = str(exc)
        finally:
            if file is not None:
                file.close()
