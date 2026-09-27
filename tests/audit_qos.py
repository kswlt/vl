"""Read-only runtime checks for observable Zenoh QoS and command/file paths."""
import argparse
import json
from pathlib import Path
import queue
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import zenoh
from pc import active_msgs_pb2 as pb


def open_client(endpoint):
    config = zenoh.Config()
    config.insert_json5("mode", '"client"')
    config.insert_json5("connect/endpoints", json.dumps([endpoint]))
    config.insert_json5("scouting/multicast/enabled", "false")
    return zenoh.open(config)


def qos(sample):
    return {
        "congestion_control": str(sample.congestion_control),
        "priority": str(sample.priority),
    }


def check_qos(sample, congestion, priority):
    assert sample.congestion_control == congestion
    assert sample.priority == priority
    return qos(sample)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--samples", type=int, default=20)
    args = parser.parse_args()
    cfg = json.loads(Path("config/pc.json").read_text(encoding="utf-8"))
    base = f"active/{cfg['sn']}"
    report = {
        "samples_per_sensor_topic": args.samples,
        "python_reliability_observable": hasattr(zenoh.Sample, "reliability"),
        "checks": {},
    }

    with open_client(cfg["image_endpoint"]) as session:
        incoming = queue.Queue()
        sub = session.declare_subscriber(base + "/image", incoming.put)
        for index in range(args.samples):
            sample = incoming.get(timeout=3)
            observed = check_qos(sample, zenoh.CongestionControl.DROP, zenoh.Priority.DATA)
            image = pb.ImageMsgArray.FromString(sample.payload.to_bytes())
            assert len(image.array) == 1 and image.array[0].format == 2
            if index == 0:
                report["checks"]["image"] = observed
        sub.undeclare()

    with open_client(cfg["pointcloud_endpoint"]) as session:
        incoming = queue.Queue()
        sub = session.declare_subscriber(base + "/pointcloud", incoming.put)
        for index in range(args.samples):
            sample = incoming.get(timeout=3)
            observed = check_qos(sample, zenoh.CongestionControl.DROP, zenoh.Priority.DATA)
            cloud = pb.LidarPointMsgArray.FromString(sample.payload.to_bytes())
            assert len(cloud.array) == 1 and cloud.array[0].header.scaler == 1000
            if index == 0:
                report["checks"]["pointcloud"] = observed
        sub.undeclare()

        command_payload = pb.SettingRequest(header=pb.Header(seq=7001)).SerializeToString()
        replies = list(session.get(
            base + "/cmd/setting", payload=command_payload, timeout=3,
            congestion_control=zenoh.CongestionControl.BLOCK,
            priority=zenoh.Priority.DATA,
        ))
        assert len(replies) == 1 and replies[0].ok is not None
        command_sample = replies[0].ok
        response = pb.SettingResponse.FromString(command_sample.payload.to_bytes())
        assert response.error.code == 0
        report["checks"]["cmd_reply"] = check_qos(
            command_sample, zenoh.CongestionControl.BLOCK, zenoh.Priority.DATA)
        report["setting"] = {
            "error_code": response.error.code,
            "lidar_fps": response.lidar_fps,
            "image_fps": response.image_fps,
            "image_format": response.image_format,
        }

        files_incoming = queue.Queue()
        files_sub = session.declare_subscriber(base + "/config_file", files_incoming.put)
        replies = list(session.get(
            base + "/config_file", timeout=3,
            congestion_control=zenoh.CongestionControl.BLOCK,
            priority=zenoh.Priority.DATA,
        ))
        assert len(replies) == 1 and replies[0].ok is not None
        reply_sample = replies[0].ok
        reply_files = pb.FileMsgArray.FromString(reply_sample.payload.to_bytes())
        assert {item.path for item in reply_files.array} >= {"config.json", "lidar_intrinsics.json"}
        report["checks"]["config_file_reply"] = check_qos(
            reply_sample, zenoh.CongestionControl.BLOCK, zenoh.Priority.DATA)

        published_sample = files_incoming.get(timeout=3)
        published_files = pb.FileMsgArray.FromString(published_sample.payload.to_bytes())
        assert {item.path: bytes(item.data) for item in published_files.array} == {
            item.path: bytes(item.data) for item in reply_files.array
        }
        report["checks"]["config_file_publisher"] = check_qos(
            published_sample, zenoh.CongestionControl.BLOCK, zenoh.Priority.DATA)
        files_sub.undeclare()

    report["result"] = "PASS"
    Path(args.output).write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
