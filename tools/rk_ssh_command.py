"""Run an explicit command on the verified RK3562 over SSH."""
import base64
import hashlib
import sys

import paramiko

HOST, USER, PASSWORD = "203.0.113.179", "root", "root"
EXPECT_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"


def fingerprint(key):
    digest = hashlib.sha256(key.asbytes()).digest()
    return "SHA256:" + base64.b64encode(digest).decode().rstrip("=")


if len(sys.argv) != 2:
    raise SystemExit("usage: rk_ssh_command.py COMMAND")

transport = paramiko.Transport((HOST, 22))
transport.connect()
assert fingerprint(transport.get_remote_server_key()) == EXPECT_FP, "fingerprint mismatch"
transport.auth_password(USER, PASSWORD)
channel = transport.open_session()
channel.exec_command(sys.argv[1])
stdout = bytearray()
stderr = bytearray()
while not (channel.exit_status_ready() and not channel.recv_ready() and not channel.recv_stderr_ready()):
    if channel.recv_ready():
        stdout.extend(channel.recv(65536))
    if channel.recv_stderr_ready():
        stderr.extend(channel.recv_stderr(65536))
stdout.extend(channel.recv(65536))
stderr.extend(channel.recv_stderr(65536))
status = channel.recv_exit_status()
sys.stdout.buffer.write(stdout)
sys.stderr.buffer.write(stderr)
channel.close()
transport.close()
raise SystemExit(status)
