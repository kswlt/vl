# -*- coding: utf-8 -*-
"""dump zenoh pointcloud 原始字节, 检查协议结构"""
import time, zenoh

conf = zenoh.Config()
conf.insert_json5("mode", '"client"')
conf.insert_json5("connect/endpoints", '["tcp/203.0.113.179:7447"]')
sess = zenoh.open(conf)
sub = sess.declare_subscriber("active/DEVICE00000001/pointcloud")
deadline = time.time() + 8
sample = None
while time.time() < deadline:
    sample = sub.recv()
    if sample is not None:
        break
    time.sleep(0.1)
if sample is None:
    print("timeout")
else:
    data = bytes(sample.payload)
    print("len=%d" % len(data))
    print("HEAD hex:", data[:120].hex(" "))
    # 打印首字节可能的 varint tag
    print("first bytes:", list(data[:30]))
sub.undeclare()
sess.close()
print("DONE")
