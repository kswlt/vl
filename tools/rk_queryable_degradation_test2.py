# -*- coding: utf-8 -*-
"""Round A: hammer config_file on current (post-test) gateway. Round B: after restart, per-queryable first/second reply."""
import base64
import hashlib
import subprocess
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


def restart():
    t = paramiko.Transport((HOST, 22)); t.connect()
    assert fp(t.get_remote_server_key()) == EXPECT_FP
    t.auth_password(USER, PASSWORD)
    ch = t.open_session(); ch.exec_command("/etc/init.d/S98zzgateway restart")
    while not ch.exit_status_ready():
        time.sleep(0.2)
    t.close()


def probe(py, key, tag):
    t0 = time.time()
    r = subprocess.run([py, PROBE, EP, key], capture_output=True, text=True, timeout=20)
    out = (r.stdout + r.stderr).replace("\n", " | ").strip()
    print(f"[{time.strftime('%H:%M:%S')}] {tag} ({time.time()-t0:.1f}s): {out}", flush=True)


print("=== Round A: current gateway, config_file x3 (1.10) ===", flush=True)
for i in (1, 2, 3):
    probe(PY110, "VENDOR00000001/config_file", f"A:1.10 config_file #{i}")
    time.sleep(1.0)

print("=== restart gateway ===", flush=True)
restart()
time.sleep(12)
print("=== Round B: fresh gateway ===", flush=True)
probe(PY110, "VENDOR00000001/config_file", "B:1.10 config_file #1")
time.sleep(1.0)
probe(PY110, "VENDOR00000001/config_file", "B:1.10 config_file #2")
time.sleep(1.0)
probe(PY190, "LEGACY00000001/cmd/setting", "B:1.9  cmd/setting #1")
time.sleep(1.0)
probe(PY190, "LEGACY00000001/cmd/setting", "B:1.9  cmd/setting #2")
