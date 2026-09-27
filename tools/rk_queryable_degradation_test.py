# -*- coding: utf-8 -*-
"""Restart RK gateway, then probe queryables with alternating clients to map degradation."""
import base64
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

import paramiko

ROOT = Path(__file__).resolve().parents[1]
HOST, USER, PASSWORD = "203.0.113.179", "root", "root"
EXPECT_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"
PY110 = str(ROOT / ".venv/Scripts/python.exe")
PY190 = str(ROOT / ".venv_zenoh_190/Scripts/python.exe")
PROBE = str(ROOT / "tools/probe_zenoh_get.py")
EP = "tcp/203.0.113.179:7447#bind=203.0.113.200:0"


def fp(key):
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")


def ssh_run(t, cmd):
    ch = t.open_session(); ch.exec_command(cmd)
    data = b""
    while not (ch.exit_status_ready() and not ch.recv_ready()):
        if ch.recv_ready(): data += ch.recv(65536)
    data += ch.recv(65536)
    ch.close()
    return data.decode(errors="replace").strip()


def probe(py, key, tag):
    t0 = time.time()
    r = subprocess.run([py, PROBE, EP, key], capture_output=True, text=True, timeout=20)
    out = (r.stdout + r.stderr).replace("\n", " | ").strip()
    print(f"[{time.strftime('%H:%M:%S')}] {tag}: {out}", flush=True)


t = paramiko.Transport((HOST, 22)); t.connect()
assert fp(t.get_remote_server_key()) == EXPECT_FP
t.auth_password(USER, PASSWORD)
print(ssh_run(t, "/etc/init.d/S98zzgateway restart"), flush=True)
t.close()
print("gateway restarted, waiting 12s...", flush=True)
time.sleep(12)

steps = [
    (PY110, "LEGACY00000001/cmd/setting", "1.10 cmd/setting #1"),
    (PY110, "LEGACY00000001/cmd/setting", "1.10 cmd/setting #2"),
    (PY190, "LEGACY00000001/cmd/setting", "1.9  cmd/setting #3"),
    (PY110, "VENDOR00000001/config_file",   "1.10 config_file  #4"),
    (PY190, "LEGACY00000001/cmd/setting", "1.9  cmd/setting #5"),
    (PY110, "LEGACY00000001/cmd/setting", "1.10 cmd/setting #6"),
]
for py, key, tag in steps:
    probe(py, key, tag)
    time.sleep(1.5)
