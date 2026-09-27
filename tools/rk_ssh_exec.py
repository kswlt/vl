# -*- coding: utf-8 -*-
"""Run read-only diagnostics on RK board over SSH with host-key verification."""
import base64
import hashlib
import sys

import paramiko

HOST, USER, PASSWORD = "203.0.113.179", "root", "root"
EXPECT_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"

CMDS = [
    "ps | grep -E 'xt_' | grep -v grep",
    "ls -la /userdata/xtapp/*.log",
    "tail -n 40 /userdata/xtapp/xt_gateway.log",
    "sha256sum /userdata/xtapp/xt_gateway /userdata/xtapp/xt_radar /userdata/xtapp/xt_camera",
    "ls -la /tmp/xt_*.sock 2>/dev/null",
    "cat /userdata/xtapp/config.json",
    "pidof xt_gateway xt_radar xt_camera",
]


def fp(key):
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")


t = paramiko.Transport((HOST, 22))
t.connect()
k = t.get_remote_server_key()
assert fp(k) == EXPECT_FP, "fingerprint mismatch"
t.auth_password(USER, PASSWORD)
assert t.is_authenticated()
for cmd in CMDS:
    ch = t.open_session()
    ch.exec_command(cmd)
    data = b""
    while not (ch.exit_status_ready() and not ch.recv_ready()):
        if ch.recv_ready():
            data += ch.recv(65536)
    data += ch.recv(65536)
    print(f"$ {cmd}")
    print(data.decode(errors="replace").rstrip())
    print("-" * 70)
    ch.close()
t.close()
