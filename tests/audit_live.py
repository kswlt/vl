"""Independent generated-Protobuf/Pillow receiver and Foxglove wire audit.

No dependency on pc.gateway conversion or its counters. Outputs are measurements,
not a blanket protocol compliance verdict. A seq gap is not necessarily a packet
loss: the legacy board shares its pointcloud sequence with command responses.
"""
import argparse
import asyncio
import base64
from collections import Counter
import hashlib
import io
import json
from pathlib import Path
import queue
import struct
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import aiohttp
from google.protobuf import descriptor_pb2, descriptor_pool, message_factory
from PIL import Image
import zenoh
from pc import active_msgs_pb2 as pb


def percentile(values, fraction):
    if not values:
        return None
    values = sorted(values)
    i = (len(values) - 1) * fraction
    lo = int(i)
    hi = min(lo + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (i - lo)


class Stats:
    def __init__(self):
        self.times, self.sizes, self.counts, self.seqs, self.stamps = [], [], [], [], []
        self.errors = Counter()
        self.dimensions, self.formats, self.lengths, self.scalers = Counter(), Counter(), Counter(), Counter()
        self.hashes, self.jpeg_sizes = [], []
        self.last_seq, self.last_stamp = None, None
        self.ended_mono = None

    def header(self, header):
        if self.last_seq is not None:
            delta = (header.seq - self.last_seq) & 0xffffffff
            if 1 < delta < 0x80000000:
                self.errors['seq_gaps'] += delta - 1
            elif delta != 1:
                self.errors['seq_repeat_or_reset'] += 1
            if header.stamp <= self.last_stamp:
                self.errors['timestamp_nonmono'] += 1
        self.last_seq, self.last_stamp = header.seq, header.stamp
        self.seqs.append(header.seq)
        self.stamps.append(header.stamp)
        self.scalers[str(header.scaler)] += 1

    def jpeg(self, jpeg, width=None, height=None):
        self.jpeg_sizes.append(len(jpeg))
        if jpeg[:2] != b'\xff\xd8' or jpeg[-2:] != b'\xff\xd9':
            self.errors['soi_eoi_bad'] += 1
        try:
            with Image.open(io.BytesIO(jpeg)) as im:
                im.load()
                self.dimensions[f'{im.width}x{im.height}'] += 1
                if im.format != 'JPEG':
                    self.errors['jpeg_format_bad'] += 1
                if width is not None and im.size != (width, height):
                    self.errors['dimension_mismatch'] += 1
        except Exception:
            self.errors['jpeg_decode_fail'] += 1
        self.hashes.append(hashlib.sha256(jpeg).hexdigest())

    def report(self):
        intervals = [(b - a) * 1000 for a, b in zip(self.times, self.times[1:])]
        duration = self.times[-1] - self.times[0] if len(self.times) > 1 else 0
        tail_ms = max(0, ((self.ended_mono or time.monotonic())-self.times[-1])*1000) if self.times else None
        return dict(frames=len(self.times), received_span_s=duration,
                    tail_silence_ms=tail_ms,
                    stream_present_at_end=bool(self.times) and tail_ms < 1000,
                    avg_hz=(len(self.times)-1)/duration if duration else 0,
                    p50_ms=percentile(intervals,.5), p95_ms=percentile(intervals,.95),
                    p99_ms=percentile(intervals,.99), max_ms=max(intervals,default=None),
                    gaps_over_500ms=sum(t>500 for t in intervals),
                    bytes_avg=sum(self.sizes)/len(self.sizes) if self.sizes else 0,
                    jpeg_bytes_avg=sum(self.jpeg_sizes)/len(self.jpeg_sizes) if self.jpeg_sizes else None,
                    count_min=min(self.counts,default=None),count_max=max(self.counts,default=None),
                    dimensions=dict(self.dimensions),formats=dict(self.formats),array_lengths=dict(self.lengths),
                    scalers=dict(self.scalers),seq_first=self.seqs[0] if self.seqs else None,
                    seq_last=self.last_seq,stamp_first=self.stamps[0] if self.stamps else None,
                    stamp_last=self.last_stamp,
                    errors={key:self.errors[key] for key in [
                        'parse_fail','array_len_bad','missing_header','format_bad','soi_eoi_bad',
                        'jpeg_decode_fail','dimension_mismatch','scaler_bad','packed_bad',
                        'seq_gaps','seq_repeat_or_reset','timestamp_nonmono','input_queue_full',
                        'jpeg_format_bad','connect_fail']})


def write_json(path, value):
    path.write_text(json.dumps(value,indent=2,ensure_ascii=False),encoding='utf-8')


def direct_worker(kind, cfg, seconds, directory, stats, ready):
    q = queue.Queue(maxsize=128)
    def receive(sample):
        try:
            q.put_nowait((sample.payload.to_bytes(), time.monotonic()))
        except queue.Full:
            stats.errors['input_queue_full'] += 1
    c = zenoh.Config()
    c.insert_json5('mode', json.dumps('client'))
    c.insert_json5('connect/endpoints',json.dumps([cfg[f'{kind}_endpoint']]))
    c.insert_json5('scouting/multicast/enabled','false')
    try:
        session = zenoh.open(c)
    except Exception as exc:
        stats.errors['connect_fail'] += 1
        ready.set()
        write_json(directory/f'{kind}-error.json',dict(error=str(exc)))
        return
    with session:
        sub = session.declare_subscriber(f"active/{cfg['sn']}/{kind}",receive)
        ready.set()
        deadline=time.monotonic()+seconds
        with (directory/f'{kind}-arrivals.csv').open('w',encoding='ascii') as trace:
            trace.write('receive_monotonic_s,seq,source_stamp_ns,bytes,count\n')
            while time.monotonic()<deadline:
                try:
                    payload, received=q.get(timeout=min(.25,max(.001,deadline-time.monotonic())))
                except queue.Empty:
                    continue
                stats.times.append(received)
                stats.sizes.append(len(payload))
                arr=pb.ImageMsgArray() if kind=='image' else pb.LidarPointMsgArray()
                try:
                    arr.ParseFromString(payload)
                except Exception:
                    stats.errors['parse_fail']+=1
                    continue
                stats.lengths[str(len(arr.array))]+=1
                if len(arr.array)!=1:
                    stats.errors['array_len_bad']+=1
                for msg in arr.array:
                    if not msg.HasField('header'):
                        stats.errors['missing_header']+=1
                    stats.header(msg.header)
                    if kind=='image':
                        stats.formats[str(msg.format)]+=1
                        if msg.format!=2:stats.errors['format_bad']+=1
                        stats.jpeg(msg.data,msg.width,msg.height)
                        count=len(msg.data)
                    else:
                        if msg.header.scaler!=1000:stats.errors['scaler_bad']+=1
                        count=len(msg.points)
                        stats.counts.append(count)
                        # Independently build the expected display representation, every point in order.
                        packed=b''.join(struct.pack('<dddIfIi',p.x/1000,p.y/1000,p.z/1000,
                                                    p.rgbi,float(p.rgbi&255),p.ring,p.offset) for p in msg.points)
                        stats.hashes.append(hashlib.sha256(packed).hexdigest())
                    trace.write(f'{received:.9f},{msg.header.seq},{msg.header.stamp},{len(payload)},{count}\n')
                n=len(stats.times)
                if n==1:
                    (directory/f'{kind}-first.pb').write_bytes(payload)
                    if kind=='image' and arr.array:
                        (directory/'camera-first.jpg').write_bytes(arr.array[0].data)
                if n in (100,1000):
                    write_json(directory/f'{kind}-{n}.json',stats.report())
                    print(f'{kind} {n}: '+json.dumps(stats.report()),flush=True)
        stats.ended_mono = time.monotonic()
        sub.undeclare()


def message_type(channel):
    desc=descriptor_pb2.FileDescriptorSet.FromString(base64.b64decode(channel['schema']))
    pool=descriptor_pool.DescriptorPool()
    pending=list(desc.file)
    while pending:
        failures=[]
        for file in pending:
            try:pool.Add(file)
            except Exception:failures.append(file)
        if len(failures)==len(pending):raise ValueError('unresolvable advertised schema')
        pending=failures
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(channel['schemaName']))


async def websocket_audit(url,seconds,results,directory):
    mapping={}
    async with aiohttp.ClientSession() as client:
        async with client.ws_connect(url,protocols=['foxglove.sdk.v1','foxglove.websocket.v1'],heartbeat=10,max_msg_size=8_000_000) as ws:
            deadline=time.monotonic()+seconds
            while time.monotonic()<deadline:
                try:event=await asyncio.wait_for(ws.receive(),min(2,deadline-time.monotonic()))
                except asyncio.TimeoutError:continue
                if event.type==aiohttp.WSMsgType.TEXT:
                    msg=json.loads(event.data)
                    if msg.get('op')=='advertise':
                        subscriptions=[]
                        for ch in msg['channels']:
                            kind=ch['topic'].split('/')[-1]
                            if kind not in results:continue
                            mapping[ch['id']]=(kind,message_type(ch))
                            subscriptions.append(dict(id=ch['id'],channelId=ch['id']))
                        await ws.send_json(dict(op='subscribe',subscriptions=subscriptions))
                elif event.type==aiohttp.WSMsgType.BINARY:
                    raw=event.data
                    if len(raw)<13 or raw[0]!=1:continue
                    sid,logtime=struct.unpack_from('<IQ',raw,1)
                    kind,cls=mapping[sid]
                    stats=results[kind]
                    stats.times.append(time.monotonic())
                    stats.sizes.append(len(raw)-13)
                    try:
                        msg=cls.FromString(raw[13:])
                        if kind=='image':
                            if msg.format!='jpeg':stats.errors['format_bad']+=1
                            stats.jpeg(msg.data)
                        else:
                            if msg.point_stride!=40 or len(msg.data)%40:stats.errors['packed_bad']+=1
                            stats.counts.append(len(msg.data)//max(1,msg.point_stride))
                            stats.hashes.append(hashlib.sha256(msg.data).hexdigest())
                    except Exception:
                        stats.errors['parse_fail']+=1
                    if len(stats.times) in (100,1000):
                        write_json(directory/f'foxglove-{kind}-{len(stats.times)}.json',stats.report())
                elif event.type in (aiohttp.WSMsgType.CLOSE,aiohttp.WSMsgType.CLOSED,aiohttp.WSMsgType.ERROR):
                    raise ConnectionError(f'WebSocket closed during audit: {event.type}')


async def main(args):
    directory=Path(args.output)
    directory.mkdir(parents=True,exist_ok=False)
    cfg=json.loads(Path(args.config).read_text(encoding='utf-8'))
    started=time.time()
    direct={kind:Stats() for kind in ('image','pointcloud')}
    wire={kind:Stats() for kind in direct}
    threads=[]
    for kind in direct:
        ready=threading.Event()
        thread=threading.Thread(target=direct_worker,args=(kind,cfg,args.seconds,directory,direct[kind],ready))
        thread.start()
        threads.append(thread)
        await asyncio.to_thread(ready.wait,10)
    error=None
    try:
        await websocket_audit(args.ws,args.seconds,wire,directory)
    except Exception as exc:
        error=repr(exc)
    for stats in wire.values():
        stats.ended_mono=time.monotonic()
    for thread in threads:
        while thread.is_alive():
            await asyncio.sleep(.2)
    report=dict(started_unix=started,ended_unix=time.time(),requested_seconds=args.seconds,
                websocket_error=error,direct={k:v.report() for k,v in direct.items()},
                foxglove={k:v.report() for k,v in wire.items()})
    for kind in direct:
        known=set(direct[kind].hashes)
        report['foxglove'][kind]['identical_to_direct_frames']=sum(h in known for h in wire[kind].hashes)
        interior = [h for t,h in zip(wire[kind].times,wire[kind].hashes)
                    if direct[kind].times and direct[kind].times[0]+1<t<direct[kind].times[-1]-1]
        report['foxglove'][kind]['interior_compared']=len(interior)
        report['foxglove'][kind]['interior_mismatch']=sum(h not in known for h in interior)
        report['foxglove'][kind]['comparison_note']='Unmatched boundary frames may precede/follow direct subscription; no resampling or JPEG recompression.'
        write_json(directory/f'{kind}-final.json',direct[kind].report())
    write_json(directory/'summary.json',report)
    print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--seconds',type=float,default=60)
    parser.add_argument('--config',default='config/pc.json')
    parser.add_argument('--ws',default='ws://127.0.0.1:8766')
    parser.add_argument('--output',required=True)
    asyncio.run(main(parser.parse_args()))
