# -*- coding: utf-8 -*-
import base64, hashlib
import paramiko

HOST, USER, PASSWORD = "203.0.113.179", "root", "root"
EXPECT_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"

CMDS = [
    "PID=$(pidof xt_gateway); echo PID=$PID; ls -l /proc/$PID/fd/1 /proc/$PID/fd/2",
    "PID=$(pidof xt_gateway); for t in /proc/$PID/task/*; do echo -n \"tid $(basename $t) state=\"; awk '{print $3}' $t/stat; done",
    "PID=$(pidof xt_gateway); cat /proc/$PID/cmdline | tr '\\0' ' '; echo",
    "PID=$(pidof xt_gateway); tr '\\0' ' ' < /proc/$PID/environ; echo",
    "ls -la /userdata/xtapp/release-legacy-compat/ 2>/dev/null | head -30",
    "grep -a -E 'queryable|cmd|ready|z_open|read_task' /userdata/xtapp/xt_gateway.log | tail -n 20",
]

def fp(key):
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")

t = paramiko.Transport((HOST, 22)); t.connect()
assert fp(t.get_remote_server_key()) == EXPECT_FP
t.auth_password(USER, PASSWORD)
for cmd in CMDS:
    ch = t.open_session(); ch.exec_command(cmd)
    data = b""
    while not (ch.exit_status_ready() and not ch.recv_ready()):
        if ch.recv_ready(): data += ch.recv(65536)
    data += ch.recv(65536)
    print(f"$ {cmd}"); print(data.decode(errors="replace").rstrip()); print("-" * 70)
    ch.close()
t.close()
