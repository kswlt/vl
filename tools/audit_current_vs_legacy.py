#!/usr/bin/env python3
"""Compare a current RK capture with preserved raw legacy protobuf payloads."""
import argparse, collections, json, math, statistics, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from pc import active_msgs_pb2 as pb

def q(values, p):
    values = sorted(values)
    if not values: return None
    return values[min(len(values)-1, round((len(values)-1)*p))]

def nal_types(data):
    out=[]; i=0
    while i+3<len(data):
        if data[i:i+4]==b'\0\0\0\1': j=i+4; i=j+1
        elif data[i:i+3]==b'\0\0\1': j=i+3; i=j+1
        else: i+=1; continue
        if j<len(data): out.append(data[j]&31)
    return out

def seq_gaps(seqs):
    return sum(max(0,b-a-1) for a,b in zip(seqs,seqs[1:]))

def timing(stamps):
    ds=[b-a for a,b in zip(stamps,stamps[1:])]
    return {"count":len(stamps),"nonmonotonic":sum(d<=0 for d in ds),"delta_us":{"min":min(ds) if ds else None,"p50":q(ds,.5),"p95":q(ds,.95),"max":max(ds) if ds else None},"hz":((len(stamps)-1)*1e6/(stamps[-1]-stamps[0])) if len(stamps)>1 and stamps[-1]>stamps[0] else None}

def analyze(root):
    manifest=json.loads((root/'manifest.json').read_text(encoding='utf-8'))
    imgs=[]; image_outer=[]
    for path in sorted((root/'raw/image').glob('*.bin')):
        raw=path.read_bytes(); image_outer.append(len(raw)); imgs.extend(pb.ImageMsgArray.FromString(raw).array)
    pcs=[]; pc_outer=[]
    for path in sorted((root/'raw/pointcloud').glob('*.bin')):
        raw=path.read_bytes(); pc_outer.append(len(raw)); pcs.extend(pb.LidarPointMsgArray.FromString(raw).array)
    imus=[]
    for path in sorted((root/'raw/imu').glob('*.bin')) if (root/'raw/imu').exists() else []:
        imus.extend(pb.ImuMsgArray.FromString(path.read_bytes()).array)
    patterns=collections.Counter(tuple(nal_types(bytes(m.data))) for m in imgs)
    idrs=[m for m in imgs if 5 in nal_types(bytes(m.data))]
    points=[p for m in pcs[:10] for p in m.points]
    valid=[p for p in points if p.x or p.y or p.z]
    ranges=[math.sqrt(p.x*p.x+p.y*p.y+p.z*p.z)/1000 for p in valid]
    def axis(name):
        vals=[getattr(p,name)/1000 for p in valid]
        return {"min":min(vals),"p01":q(vals,.01),"p50":q(vals,.5),"p99":q(vals,.99),"max":max(vals)} if vals else {}
    image_stamps=[m.header.stamp for m in imgs]; pc_stamps=[m.header.stamp for m in pcs]
    nearest=[min(abs(s-p) for s in image_stamps) for p in pc_stamps] if image_stamps else []
    rec_offsets=[]
    for rec in manifest['records']:
        if rec.get('items'):
            rec_offsets.append(rec['received_unix_us']-rec['items'][0]['stamp'])
    setting=manifest.get('query_results',{}).get('setting',[])
    return {
      "label":manifest['label'],"sn":manifest['sn'],"topics":manifest['topic_counts'],"capture_dropped":manifest['dropped'],
      "image":{"outer_messages":len(image_outer),"outer_bytes":{"min":min(image_outer),"p50":q(image_outer,.5),"max":max(image_outer)},"batch_lengths":dict(collections.Counter(len(pb.ImageMsgArray.FromString(p.read_bytes()).array) for p in sorted((root/'raw/image').glob('*.bin')))),"frames":len(imgs),"format_values":sorted(set(m.format for m in imgs)),"dimensions":sorted(set((m.width,m.height) for m in imgs)),"header_scalers":sorted(set(m.header.scaler for m in imgs)),"seq_gaps":seq_gaps([m.header.seq for m in imgs]),"timing":timing(image_stamps),"nal_patterns":{str(k):v for k,v in patterns.most_common()},"idr_count":len(idrs),"idr_with_sps_pps":sum(7 in nal_types(bytes(m.data)) and 8 in nal_types(bytes(m.data)) for m in idrs),"aud_frames":sum(9 in nal_types(bytes(m.data)) for m in imgs),"sei_frames":sum(6 in nal_types(bytes(m.data)) for m in imgs)},
      "pointcloud":{"frames":len(pcs),"outer_bytes":{"min":min(pc_outer),"p50":q(pc_outer,.5),"max":max(pc_outer)},"array_lengths":sorted(set(len(pb.LidarPointMsgArray.FromString(p.read_bytes()).array) for p in sorted((root/'raw/pointcloud').glob('*.bin')))),"point_counts":{"min":min(len(m.points) for m in pcs),"p50":q([len(m.points) for m in pcs],.5),"max":max(len(m.points) for m in pcs),"unique":len(set(len(m.points) for m in pcs))},"zero_xyz_fraction_first10":1-len(valid)/len(points),"header_scalers":sorted(set(m.header.scaler for m in pcs)),"seq_gaps":seq_gaps([m.header.seq for m in pcs]),"timing":timing(pc_stamps),"range_m_first10":{"p50":q(ranges,.5),"p95":q(ranges,.95),"p99":q(ranges,.99),"max":max(ranges) if ranges else None},"axes_m_first10":{"x":axis('x'),"y":axis('y'),"z":axis('z')},"ring_values":sorted(set(p.ring for p in points)),"offset_values":sorted(set(p.offset for p in points))[:20]},
      "cross_stream":{"nearest_delta_us":{"min":min(nearest),"p50":q(nearest,.5),"p95":q(nearest,.95),"max":max(nearest)} if nearest else {},"receive_minus_stamp_us":{"p50":q(rec_offsets,.5),"min":min(rec_offsets),"max":max(rec_offsets)} if rec_offsets else {}},
      "imu":{"messages":len(imus),"timing":timing([m.header.stamp for m in imus]) if imus else None},
      "setting":setting,
      "config_file_replies":len(manifest.get('query_results',{}).get('config_file',[])),
    }

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--rk',type=Path,required=True);ap.add_argument('--legacy',type=Path,required=True);ap.add_argument('--output',type=Path);a=ap.parse_args()
    result={"rk":analyze(a.rk),"legacy":analyze(a.legacy)}
    text=json.dumps(result,ensure_ascii=False,indent=2)
    if a.output:a.output.write_text(text+'\n',encoding='utf-8')
    print(text)
if __name__=='__main__':main()
