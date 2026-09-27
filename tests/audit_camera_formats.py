"""Independent protocol-level JPEG/H264 subscriber and decoder-input capture."""
import argparse
import io
import json
from pathlib import Path
import queue
import subprocess
import shutil
import statistics
import sys
import time

from PIL import Image
import zenoh
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pc import active_msgs_pb2 as pb


def zenoh_client(endpoint):
    config = zenoh.Config()
    config.insert_json5("mode", '"client"')
    config.insert_json5("connect/endpoints", json.dumps([endpoint]))
    config.insert_json5("scouting/multicast/enabled", "false")
    return config


def nal_types(data):
    result = []
    at = 0
    while at + 4 <= len(data):
        if data[at:at + 4] == b"\x00\x00\x00\x01":
            result.append(data[at + 4] & 0x1f)
            at += 5
        elif data[at:at + 3] == b"\x00\x00\x01":
            result.append(data[at + 3] & 0x1f)
            at += 4
        else:
            at += 1
    return result


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, round((len(ordered) - 1) * fraction))]


def set_settings(endpoint, sn, image_format, h264_gop, h264_bitrate):
    with zenoh.open(zenoh_client(endpoint)) as session:
        request = pb.SettingRequest(header=pb.Header(seq=1), image_format=image_format or 0,
                                    h264_gop=h264_gop or 0, h264_bitrate=h264_bitrate or 0)
        replies = [pb.SettingResponse.FromString(reply.ok.payload.to_bytes()) for reply in
                   session.get(f"active/{sn}/cmd/setting", payload=request.SerializeToString(), timeout=3)
                   if reply.ok]
    if len(replies) != 1 or replies[0].error.code != 0:
        raise RuntimeError(f"setting failed: replies={replies}")
    return replies[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected", choices=("jpeg", "h264"), required=True)
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--endpoint")
    parser.add_argument("--setting-endpoint")
    parser.add_argument("--set-format", type=int, choices=(1, 2))
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--h264-gop", type=int)
    parser.add_argument("--h264-bitrate", type=int, help="Protocol value in Mbps")
    args = parser.parse_args()
    config = json.loads(Path("config/pc.json").read_text(encoding="utf-8"))
    endpoint = args.endpoint or config["image_endpoint"]
    sn = config["sn"]
    report = {"expected": args.expected, "requested_frames": args.frames, "endpoint": endpoint}
    if args.set_format or args.h264_gop or args.h264_bitrate:
        response = set_settings(args.setting_endpoint or config["pointcloud_endpoint"], sn,
                                args.set_format, args.h264_gop, args.h264_bitrate)
        report["setting"] = {
            "error": response.error.code, "image_format": response.image_format,
            "image_fps": response.image_fps, "h264_gop": response.h264_gop,
            "h264_bitrate": response.h264_bitrate, "parameter": json.loads(response.parameter),
        }
        time.sleep(3)

    received = queue.Queue(maxsize=256)
    queue_drops = 0

    def callback(sample):
        nonlocal queue_drops
        try:
            received.put_nowait((time.monotonic(), sample.payload.to_bytes()))
        except queue.Full:
            queue_drops += 1

    times, seqs, stamps, sizes = [], [], [], []
    errors = {"protobuf": 0, "array_length": 0, "format": 0, "dimensions": 0,
              "jpeg": 0, "annexb": 0, "missing_vcl": 0}
    types = {}
    decoder_stream = bytearray()
    decoder_started = False
    expected_format = 2 if args.expected == "jpeg" else 3
    deadline = time.monotonic() + max(20, args.frames / 7 + 15)
    with zenoh.open(zenoh_client(endpoint)) as session:
        subscriber = session.declare_subscriber(f"active/{sn}/image", callback)
        while len(times) < args.frames and time.monotonic() < deadline:
            try:
                arrived, payload = received.get(timeout=.5)
            except queue.Empty:
                continue
            try:
                array = pb.ImageMsgArray.FromString(payload)
            except Exception:
                errors["protobuf"] += 1
                continue
            if len(array.array) != 1:
                errors["array_length"] += 1
                continue
            message = array.array[0]
            if message.format != expected_format:
                errors["format"] += 1
                continue
            if (message.width, message.height) != (args.width, args.height):
                errors["dimensions"] += 1
            if args.expected == "jpeg":
                try:
                    image = Image.open(io.BytesIO(message.data))
                    image.verify()
                    if image.format != "JPEG" or image.size != (message.width, message.height):
                        raise ValueError("JPEG metadata mismatch")
                except Exception:
                    errors["jpeg"] += 1
            else:
                current_types = nal_types(message.data)
                for value in current_types:
                    types[str(value)] = types.get(str(value), 0) + 1
                if not message.data.startswith((b"\x00\x00\x01", b"\x00\x00\x00\x01")):
                    errors["annexb"] += 1
                if not any(1 <= value <= 5 for value in current_types):
                    errors["missing_vcl"] += 1
                if 5 in current_types and 7 in current_types and 8 in current_types:
                    decoder_started = True
                if decoder_started:
                    decoder_stream.extend(message.data)
            times.append(arrived)
            seqs.append(message.header.seq)
            stamps.append(message.header.stamp)
            sizes.append(len(message.data))
        subscriber.undeclare()

    intervals = [(right - left) * 1000 for left, right in zip(times, times[1:])]
    seq_gaps = sum(max(0, right - left - 1) for left, right in zip(seqs, seqs[1:]))
    report.update({
        "received": len(times), "queue_drops": queue_drops, "errors": errors,
        "seq_monotonic": all(right > left for left, right in zip(seqs, seqs[1:])),
        "timestamp_monotonic": all(right > left for left, right in zip(stamps, stamps[1:])),
        "seq_gaps": seq_gaps, "average_hz": ((len(times) - 1) / (times[-1] - times[0])) if len(times) > 1 else 0,
        "interval_ms": {"p50": percentile(intervals, .50), "p95": percentile(intervals, .95),
                        "p99": percentile(intervals, .99), "max": max(intervals) if intervals else None},
        "bytes": {"mean": statistics.fmean(sizes) if sizes else 0,
                  "min": min(sizes) if sizes else 0, "max": max(sizes) if sizes else 0},
        "nal_types": types,
    })
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if args.expected == "h264":
        stream_path = output.with_suffix(".h264")
        stream_path.write_bytes(decoder_stream)
        report["decoder_stream"] = str(stream_path)
        report["decoder_stream_bytes"] = len(decoder_stream)
        ffprobe = shutil.which("ffprobe")
        if ffprobe and decoder_stream:
            probe = subprocess.run([ffprobe, "-v", "error", "-f", "h264", "-show_streams", str(stream_path)],
                                   capture_output=True, text=True)
            report["ffprobe_ok"] = probe.returncode == 0
            report["ffprobe_output"] = probe.stdout[-2000:]
        else:
            report["ffprobe_ok"] = None
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    if len(times) != args.frames or queue_drops or any(errors.values()) or seq_gaps or \
            not report["seq_monotonic"] or not report["timestamp_monotonic"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
