# -*- coding: utf-8 -*-
import base64, hashlib, time
import paramiko

HOST, USER, PASSWORD = "203.0.113.179", "root", "root"
EXPECT_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"

def fp(key):
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")

t = paramiko.Transport((HOST, 22)); t.connect()
assert fp(t.get_remote_server_key()) == EXPECT_FP
t.auth_password(USER, PASSWORD)

def run(cmd, wait=0):
    ch = t.open_session(); ch.exec_command(cmd)
    data = b""
    while not (ch.exit_status_ready() and not ch.recv_ready()):
        if ch.recv_ready(): data += ch.recv(65536)
    data += ch.recv(65536)
    print(f"$ {cmd}"); print(data.decode(errors="replace").rstrip()); print("-" * 70)
    ch.close()
    if wait: time.sleep(wait)

run("/etc/init.d/S98zzgateway restart", wait=12)
run("pidof xt_gateway xt_radar xt_camera")
run("tail -n 15 /userdata/xtapp/xt_gateway.log")
t.close()
