import io
import queue
import struct
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from PIL import Image
from pc import active_msgs_pb2 as pb
from pc.gateway import Source
from pc.recorder import RawRecorder


class GatewayTests(unittest.TestCase):
    def source(self, kind):
        cfg = dict(sn="test", image_endpoint="tcp/127.0.0.1:1",
                   pointcloud_endpoint="tcp/127.0.0.1:1", stale_seconds=3)
        with patch("pc.gateway.channels.CompressedImageChannel"), patch("pc.gateway.channels.PointCloudChannel"):
            source = Source(kind, cfg, lambda: None)
        self.addCleanup(source.close)
        return source

    def item(self, payload):
        return payload, time.monotonic(), time.time_ns()

    def displayed(self, source):
        deadline = time.monotonic() + 1
        while source.channel.log.call_count == 0 and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertGreater(source.channel.log.call_count, 0)
        return source.channel.log.call_args.args[0]

    def test_original_jpeg_and_array_retained(self):
        data = io.BytesIO()
        Image.new("RGB", (32, 16), "red").save(data, format="JPEG")
        arr = pb.ImageMsgArray()
        arr.array.add(header=pb.Header(seq=1, stamp=50), format=2,
                      width=32, height=16, data=data.getvalue())
        source = self.source("image")
        payload = arr.SerializeToString()
        with patch("pc.gateway.fg.CompressedImage", side_effect=lambda **kw: SimpleNamespace(**kw)):
            source.process(self.item(payload))
        self.assertEqual(source.latest["payload"], payload)
        self.assertEqual(source.latest["data"], data.getvalue())
        self.assertEqual(self.displayed(source).data, data.getvalue())

    def test_reject_wrong_array_and_incomplete_jpeg(self):
        source = self.source("image")
        with self.assertRaises(ValueError):
            source.process(self.item(b""))
        arr = pb.ImageMsgArray()
        arr.array.add(header=pb.Header(seq=1), format=2, data=b"\xff\xd8broken")
        with self.assertRaises(ValueError):
            source.process(self.item(arr.SerializeToString()))
        self.assertIsNone(source.latest)

    def test_all_points_units_and_signed_offsets(self):
        arr = pb.LidarPointMsgArray()
        msg = arr.array.add(header=pb.Header(seq=1, stamp=100, scaler=1000))
        for i in range(10000):
            msg.points.add(x=-i, y=i, z=500, rgbi=0x12345678, ring=5, offset=-7)
        source = self.source("pointcloud")
        with patch("pc.gateway.fg.PointCloud", side_effect=lambda **kw: SimpleNamespace(**kw)):
            source.process(self.item(arr.SerializeToString()))
        output = self.displayed(source)
        self.assertEqual(len(output.data), 10000 * 40)
        self.assertEqual(struct.unpack_from("<dddIfIi", output.data, 9999*40),
                         (-9.999, 9.999, .5, 0x12345678, 120., 5, -7))

    def test_receive_queue_is_bounded_and_counted(self):
        source = self.source("image")
        for i in range(8):
            sample = Mock()
            sample.payload.to_bytes.return_value = bytes([i])
            source.receive(sample)
        self.assertEqual(source.pending.qsize(), 4)
        self.assertEqual(source.stats["queue_dropped"], 4)
        self.assertEqual(source.pending.get_nowait()[0], b"\x04")

    def test_silent_source_keeps_subscription(self):
        source = self.source("pointcloud")
        source.cfg["reconnect_seconds"] = 0
        calls = 0
        def empty(*args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 5:
                source.stop.set()
            raise queue.Empty
        session = Mock()
        session.is_closed.return_value = False
        with patch("pc.gateway.zenoh.open", return_value=session) as opened, patch.object(source.pending,"get",side_effect=empty):
            source.thread.start()
            source.thread.join(1)
            source.stop.set()
            source.thread.join(2)
            self.assertFalse(source.thread.is_alive())
        self.assertEqual(opened.call_count, 1)
        self.assertEqual(source.stats["reconnects"], 0)
        self.assertEqual(source.stats["stale_events"], 1)
        session.close.assert_called_once()

    def test_raw_recording_is_exact_and_leaves_other_files(self):
        with tempfile.TemporaryDirectory() as temp:
            unrelated = Path(temp) / "user.bin"
            unrelated.write_bytes(b"keep")
            recorder = RawRecorder(temp)
            recorder.thread.start()
            recorder.submit(123, b"protobuf")
            recorder.stop.set()
            recorder.thread.join(2)
            self.assertFalse(recorder.thread.is_alive())
            files = list(Path(temp).glob("pointcloud-*.bin"))
            self.assertEqual(len(files), 1)
            self.assertEqual(files[0].read_bytes(), struct.pack("<QI",123,8)+b"protobuf")
            self.assertEqual(unrelated.read_bytes(), b"keep")
            self.assertEqual(recorder.status()["written"], 1)


if __name__ == "__main__":
    unittest.main()
