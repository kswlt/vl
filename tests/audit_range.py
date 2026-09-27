"""Read-only distance/intensity statistics from the actual Zenoh point cloud."""
import argparse
import json
import math
from pathlib import Path
import queue
import statistics
import time

import zenoh
from pc import active_msgs_pb2 as pb


def distribution(values):
    if not values:
        return None
    values = sorted(values)
    def percentile(p):
        index = (len(values)-1)*p
        lo, hi = math.floor(index), math.ceil(index)
        return values[lo] + (values[hi]-values[lo])*(index-lo)
    return dict(min=values[0], p50=percentile(.5), p95=percentile(.95),
                p99=percentile(.99), max=values[-1])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--frames', type=int, default=50)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    cfg = json.loads(Path('config/pc.json').read_text(encoding='utf-8'))
    config = zenoh.Config()
    config.insert_json5('mode', '"client"')
    config.insert_json5('connect/endpoints', json.dumps([cfg['pointcloud_endpoint']]))
    config.insert_json5('scouting/multicast/enabled', 'false')
    incoming = queue.Queue(maxsize=100)
    drops = []
    def receive(sample):
        try:
            incoming.put_nowait(sample.payload.to_bytes())
        except queue.Full:
            drops.append(1)
    ranges, intensities, frames = [], [], []
    started = time.time()
    with zenoh.open(config) as session:
        subscriber = session.declare_subscriber(f"active/{cfg['sn']}/pointcloud", receive)
        deadline = time.monotonic() + max(15, args.frames/4)
        while len(frames) < args.frames and time.monotonic() < deadline:
            try:
                payload = incoming.get(timeout=.5)
            except queue.Empty:
                continue
            array = pb.LidarPointMsgArray.FromString(payload)
            assert len(array.array) == 1
            msg = array.array[0]
            assert msg.header.scaler == 1000
            distances = [math.sqrt(p.x*p.x+p.y*p.y+p.z*p.z)/1000 for p in msg.points]
            values = [p.rgbi & 255 for p in msg.points]
            ranges.extend(distances)
            intensities.extend(values)
            frames.append(dict(seq=msg.header.seq, points=len(msg.points),
                               distance_m=distribution(distances), intensity=distribution(values)))
        subscriber.undeclare()
    report = dict(started_unix=started, ended_unix=time.time(), frames=len(frames),
                  points=len(ranges), queue_drops=len(drops), distance_m=distribution(ranges),
                  intensity=distribution(intensities), samples=frames,
                  note='Observed scene range, not a validated hardware range limit; no point filtering.')
    Path(args.output).write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='samples'}, indent=2))
    assert len(frames)==args.frames and not drops


if __name__ == '__main__':
    main()
