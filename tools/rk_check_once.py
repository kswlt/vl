# -*- coding: utf-8 -*-
import base64, hashlib, sys
import paramiko

HOST, USER, PASSWORD = "203.0.113.179", "root", "root"
EXPECT_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"

def fp(key):
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")

t = paramiko.Transport((HOST, 22))
t.connect()
k = t.get_remote_server_key()
got = fp(k)
print(f"[fingerprint] {got}")
assert got == EXPECT_FP, "FINGERPRINT MISMATCH"
print("[fingerprint] MATCH")
t.auth_password(USER, PASSWORD)
assert t.is_authenticated()
print("[auth] OK")

for cmd in [
    "echo MODEL=$(cat /proc/device-tree/model | tr -d '\\0')",
    "hostname",
    "ifconfig eth1 2>/dev/null | head -n 3",
    "ip -4 addr show eth1 2>/dev/null | grep inet",
    "ls /userdata/xtapp/xt_gateway >/dev/null 2>&1 && echo xtapp_present=yes",
    "pidof xt_gateway xt_camera xt_radar 2>/dev/null | tr ' ' '\n' | sort -u | grep -c . | xargs echo xt_procs=",
]:
    ch = t.open_session()
    ch.exec_command(cmd)
    data = b""
    while not (ch.exit_status_ready() and not ch.recv_ready()):
        if ch.recv_ready():
            data += ch.recv(65536)
    data += ch.recv(65536)
    print(f"$ {cmd}")
    print(data.decode(errors="replace").rstrip())
    print("-" * 50)
    ch.close()
t.close()
