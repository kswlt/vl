# -*- coding: utf-8 -*-
"""订阅 zenoh pointcloud, 解析 protobuf 坐标分布, 验证 vizd 网格参数"""
import time, zenoh

def read_varint(buf, pos):
    v = 0; sh = 0
    while True:
        b = buf[pos]; pos += 1
        v |= (b & 0x7f) << sh
        if not (b & 0x80): break
        sh += 7
    return v, pos

def zigzag(v):
    return (v >> 1) ^ -(v & 1)

def parse_points(data):
    pts = []
    scaler = 1000
    pos = 0; L = len(data)
    while pos < L:
        tag, pos = read_varint(data, pos)
        f = tag >> 3; w = tag & 7
        if f == 1 and w == 2:
            hl, pos = read_varint(data, pos)
            h_end = pos + hl
            while pos < h_end:
                ht, pos = read_varint(data, pos)
                hf = ht >> 3; hw = ht & 7
                if hw == 0:
                    v, pos = read_varint(data, pos)
                    if hf == 3: scaler = zigzag(v)
                elif hw == 2:
                    sl, pos = read_varint(data, pos); pos += sl
                else:
                    return pts, scaler, "bad header wire %d" % hw
        elif f == 2 and w == 2:
            pl, pos = read_varint(data, pos)
            p_end = pos + pl
            x = y = z = 0; rgbi = 0; has = 0
            while pos < p_end:
                pt, pos = read_varint(data, pos)
                pf = pt >> 3; pw = pt & 7
                if pw == 0:
                    v, pos = read_varint(data, pos)
                    if pf == 1: x = zigzag(v); has |= 1
                    elif pf == 2: y = zigzag(v); has |= 2
                    elif pf == 3: z = zigzag(v)
                    elif pf == 4: rgbi = v
                elif pw == 2:
                    sl, pos = read_varint(data, pos); pos += sl
                else:
                    return pts, scaler, "bad point wire %d" % pw
            if has == 3:
                pts.append((x/1000.0, y/1000.0, z/1000.0, rgbi))
        elif w == 0:
            _, pos = read_varint(data, pos)
        elif w == 2:
            sl, pos = read_varint(data, pos); pos += sl
        else:
            return pts, scaler, "bad wire %d" % w
    return pts, scaler, None

conf = zenoh.Config()
conf.insert_json5("mode", '"client"')
conf.insert_json5("connect/endpoints", '["tcp/203.0.113.179:7447"]')
sess = zenoh.open(conf)
sub = sess.declare_subscriber("active/DEVICE00000001/pointcloud")
for i in range(3):
    deadline = time.time() + 8
    sample = None
    while time.time() < deadline:
        sample = sub.recv()
        if sample is not None:
            break
        time.sleep(0.1)
    if sample is None:
        print("timeout"); continue
    data = bytes(sample.payload)
    pts, scaler, err = parse_points(data)
    if err:
        print("parse err:", err); continue
    if not pts:
        print("no points"); continue
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]; zs = [p[2] for p in pts]
    maxi = max(p[3] for p in pts)
    print("frame len=%d scaler=%d pts=%d x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f] inten_max=%d" % (
        len(data), scaler, len(pts), min(xs), max(xs), min(ys), max(ys), min(zs), max(zs), maxi))
sub.undeclare()
sess.close()
print("DONE")
