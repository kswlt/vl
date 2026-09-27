# -*- coding: utf-8 -*-
"""Connect to RK3562 (203.0.113.179), verify SSH host key fingerprint and board identity."""
import base64
import hashlib
import sys

import paramiko

HOST = "203.0.113.179"
PORT = 22
USER = "root"
PASSWORD = "root"
EXPECTED_ED25519_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"

CMDS = [
    "cat /proc/device-tree/model 2>/dev/null; echo",
    "uname -a",
    "ip -br addr",
    "cat /sys/class/net/eth1/address 2>/dev/null",
    "uptime",
]


def fingerprint(key):
    return "SHA256:" + base64.b64encode(
        hashlib.sha256(key.asbytes()).digest()
    ).decode().rstrip("=")


transport = paramiko.Transport((HOST, PORT))
transport.connect()
key = transport.get_remote_server_key()
fp = fingerprint(key)
print(f"offered host key: {key.get_name()} {fp}")
if key.get_name() != "ssh-ed25519" or fp != EXPECTED_ED25519_FP:
    print("HOST KEY FINGERPRINT: MISMATCH - ABORT")
    transport.close()
    sys.exit(1)
print("HOST KEY FINGERPRINT: MATCH")

transport.auth_password(username=USER, password=PASSWORD)
if not transport.is_authenticated():
    print("AUTH: FAILED")
    transport.close()
    sys.exit(1)
print("AUTH: OK (root)")
print("=" * 60)

for cmd in CMDS:
    chan = transport.open_session()
    chan.exec_command(cmd)
    out = b""
    while True:
        if chan.recv_ready():
            out += chan.recv(65536)
        if chan.exit_status_ready() and not chan.recv_ready():
            break
    out += chan.recv(65536)
    print(f"$ {cmd}")
    print(out.decode(errors="replace").rstrip())
    print("-" * 60)
    chan.close()

transport.close()
print("IDENTITY CHECK DONE")
