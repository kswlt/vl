"""Temporarily stop or restart RK acquisition services for control-plane isolation tests."""
from __future__ import annotations

import base64
import hashlib
import sys

import paramiko


HOST, USER, PASSWORD = "203.0.113.179", "root", "root"
EXPECTED_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"


def fingerprint(key: paramiko.PKey) -> str:
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode == "pause":
    command = "/etc/init.d/S99xtcamera stop; /etc/init.d/S99xtradar stop; pidof xt_gateway"
elif mode == "resume":
    command = "/etc/init.d/S99xtradar start; /etc/init.d/S99xtcamera start; sleep 4; pidof xt_gateway xt_radar xt_camera"
else:
    raise SystemExit("usage: rk_stream_pause.py pause|resume")

transport = paramiko.Transport((HOST, 22))
transport.connect()
if fingerprint(transport.get_remote_server_key()) != EXPECTED_FP:
    transport.close()
    raise SystemExit("SSH fingerprint mismatch")
transport.auth_password(USER, PASSWORD)
channel = transport.open_session()
channel.exec_command(command)
output = bytearray()
while not (channel.exit_status_ready() and not channel.recv_ready()):
    if channel.recv_ready():
        output.extend(channel.recv(65536))
while channel.recv_ready():
    output.extend(channel.recv(65536))
status = channel.recv_exit_status()
channel.close()
transport.close()
print(output.decode(errors="replace").rstrip())
raise SystemExit(status)
