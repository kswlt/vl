"""Independent generated-Protobuf config_file and persistence acceptance checks."""
import argparse
import hashlib
import json
from pathlib import Path
import queue
import time

import zenoh
from pc import active_msgs_pb2 as pb


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    parser.add_argument('--set-lidar', type=int)
    parser.add_argument('--expect-lidar', type=int, default=5)
    args = parser.parse_args()
    cfg = json.loads(Path('config/pc.json').read_text(encoding='utf-8'))
    c = zenoh.Config()
    c.insert_json5('mode', '"client"')
    c.insert_json5('connect/endpoints', json.dumps([cfg['pointcloud_endpoint']]))
    c.insert_json5('scouting/multicast/enabled', 'false')
    base = f"active/{cfg['sn']}"
    report = {'checks': [], 'files': {}}

    def check(condition, name):
        report['checks'].append({'name': name, 'pass': bool(condition)})

    with zenoh.open(c) as session:
        def query(topic, payload, cls):
            results = [cls.FromString(reply.ok.payload.to_bytes()) for reply in
                       session.get(base + '/' + topic, payload=payload, timeout=2) if reply.ok]
            if len(results) != 1:
                raise AssertionError(f'{topic}: expected one reply, received {len(results)}')
            return results[0]

        def setting(payload):
            return query('cmd/setting', payload, pb.SettingResponse)

        request = pb.SettingRequest(header=pb.Header(seq=91))
        if args.set_lidar is not None:
            request.lidar_fps = args.set_lidar
        response = setting(request.SerializeToString())
        check(response.error.code == 0, 'setting accepted')
        expected = args.set_lidar if args.set_lidar is not None else args.expect_lidar
        check(response.lidar_fps == expected, 'saved lidar_fps readback')
        check(response.image_fps == 10 and response.image_format == 2, 'JPEG 10Hz settings readback')
        report['setting'] = {name: getattr(response, name) for name in
                             ('imu_fps', 'lidar_fps', 'image_fps', 'image_format', 'h264_gop', 'h264_bitrate')}
        report['parameter'] = json.loads(response.parameter)

        for name, payload in [
            ('truncated varint', b'\x80'), ('field zero', b'\x00'),
            ('truncated header', b'\x0a\x05\x01'), ('wrong known wire', b'\x12\x00'),
            ('partial write then bad tail', pb.SettingRequest(lidar_fps=3).SerializeToString()+b'\x80'),
            ('unsupported image format', pb.SettingRequest(image_format=3).SerializeToString()),
            ('negative image format', pb.SettingRequest(image_format=-1).SerializeToString()),
            ('out of range FPS', pb.SettingRequest(image_fps=100).SerializeToString()),
        ]:
            bad = setting(payload)
            check(bad.error.code == 1 and bad.lidar_fps == expected and bad.image_fps == 10, name + ' rejected atomically')
        bad_reboot = query('cmd/reboot', b'\x80', pb.RebootResponse)
        check(bad_reboot.error.code == 1, 'malformed reboot does not restart board')

        received = queue.Queue()
        sub = session.declare_subscriber(base+'/config_file', lambda sample: received.put(sample.payload.to_bytes()))
        time.sleep(.4)
        files = query('config_file', b'', pb.FileMsgArray)
        check(len(files.array) >= 1, 'config_file query FileMsgArray not empty')
        for file in files.array:
            check(file.HasField('header') and bool(file.data), 'file header and nonempty ' + file.path)
            parsed = json.loads(file.data)
            report['files'][file.path] = {'bytes': len(file.data), 'sha256': hashlib.sha256(file.data).hexdigest(), 'json': parsed}
            if file.path == 'config.json':
                check(parsed['sensors']['lidar_fps'] == expected, 'transmitted config matches saved settings')
        check('config.json' in report['files'], 'config.json present')
        check('calibration.json' in report['files'], 'placeholder calibration present')
        check('lidar_intrinsics.json' in report['files'], 'measured lidar intrinsics present')
        check(report['files']['calibration.json']['json']['calibration_status'] == 'placeholder',
              'calibration explicitly placeholder')
        published = pb.FileMsgArray.FromString(received.get(timeout=3))
        check({x.path: bytes(x.data) for x in published.array} == {x.path: bytes(x.data) for x in files.array},
              'pub/sub FileMsgArray bytes match query reply')
        sub.undeclare()
    Path(args.output).write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps(report, indent=2, ensure_ascii=False))
    if not all(c['pass'] for c in report['checks']):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
