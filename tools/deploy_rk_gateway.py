"""Deploy the verified RK compatibility bundle after checking SSH identity."""
from __future__ import annotations

import base64
import hashlib
import time
from pathlib import Path

import paramiko


ROOT = Path(__file__).resolve().parents[1]
HOST, USER, PASSWORD = "203.0.113.179", "root", "root"
EXPECTED_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"
FILES = {
    ROOT / "build" / "board" / "xt_gateway": "/userdata/xtapp/xt_gateway",
    ROOT / "build" / "board" / "xt_radar": "/userdata/xtapp/xt_radar",
    ROOT / "build" / "board" / "xt_camera": "/userdata/xtapp/xt_camera",
    ROOT / "config" / "board.single_endpoint.json": "/userdata/xtapp/config.json",
}


def fingerprint(key: paramiko.PKey) -> str:
    digest = hashlib.sha256(key.asbytes()).digest()
    return "SHA256:" + base64.b64encode(digest).decode().rstrip("=")


def run(transport: paramiko.Transport, command: str) -> tuple[int, str]:
    channel = transport.open_session()
    channel.exec_command(command)
    data = bytearray()
    while not (channel.exit_status_ready() and not channel.recv_ready()):
        if channel.recv_ready():
            data.extend(channel.recv(65536))
    while channel.recv_ready():
        data.extend(channel.recv(65536))
    status = channel.recv_exit_status()
    channel.close()
    output = data.decode(errors="replace")
    print(f"$ {command}\n{output.rstrip()}")
    return status, output


missing = [str(path) for path in FILES if not path.is_file()]
if missing:
    raise SystemExit(f"missing deployment files: {missing}")
local_hashes = {remote: hashlib.sha256(local.read_bytes()).hexdigest()
                for local, remote in FILES.items()}

transport = paramiko.Transport((HOST, 22))
transport.connect()
actual_fp = fingerprint(transport.get_remote_server_key())
if actual_fp != EXPECTED_FP:
    transport.close()
    raise SystemExit(f"SSH fingerprint mismatch: {actual_fp}")
transport.auth_password(USER, PASSWORD)
if not transport.is_authenticated():
    raise SystemExit("SSH authentication failed")

stamp = time.strftime("%Y%m%d-%H%M%S")
backup = f"/userdata/xtapp/backup-before-rk-compat-{stamp}"
status, _ = run(
    transport,
    f"mkdir {backup} && cp -p /userdata/xtapp/xt_gateway "
    f"/userdata/xtapp/xt_radar /userdata/xtapp/xt_camera "
    f"/userdata/xtapp/config.json {backup}/",
)
if status:
    raise SystemExit("backup failed")

sftp = paramiko.SFTPClient.from_transport(transport)
for local, remote in FILES.items():
    temporary = remote + ".new"
    sftp.put(str(local), temporary)
    sftp.chmod(temporary, 0o644 if remote.endswith(".json") else 0o755)
sftp.close()

for remote, local_hash in local_hashes.items():
    status, output = run(transport, f"sha256sum {remote}.new")
    if status or output.split()[0] != local_hash:
        raise SystemExit(f"uploaded hash mismatch: {remote}")
status, _ = run(
    transport,
    "killall xt_camera xt_radar xt_gateway 2>/dev/null || true; "
    "mv /userdata/xtapp/xt_gateway.new /userdata/xtapp/xt_gateway; "
    "mv /userdata/xtapp/xt_radar.new /userdata/xtapp/xt_radar; "
    "mv /userdata/xtapp/xt_camera.new /userdata/xtapp/xt_camera; "
    "mv /userdata/xtapp/config.json.new /userdata/xtapp/config.json; "
    "rm -f /var/run/xt_gateway.pid /var/run/xt_radar.pid /var/run/xt_camera.pid; "
    "/etc/init.d/S98zzgateway start; /etc/init.d/S99xtradar start; "
    "/etc/init.d/S99xtcamera start",
)
if status:
    raise SystemExit("gateway restart failed")
time.sleep(4)
status, output = run(
    transport,
    "sha256sum /userdata/xtapp/xt_gateway /userdata/xtapp/xt_radar "
    "/userdata/xtapp/xt_camera /userdata/xtapp/config.json; "
    "pidof xt_gateway xt_radar xt_camera; "
    "tail -n 35 /userdata/xtapp/xt_gateway.log",
)
transport.close()
if status or not all(value in output for value in local_hashes.values()):
    raise SystemExit("post-deploy verification failed")
print(f"DEPLOYED {local_hashes} backup={backup} fingerprint={actual_fp}")
