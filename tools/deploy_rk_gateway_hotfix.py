"""Atomically deploy only xt_gateway to the verified RK3562."""
import base64
import argparse
import hashlib
import time
from pathlib import Path

import paramiko

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--binary", default=str(ROOT / "build" / "board" / "xt_gateway"))
args = parser.parse_args()
LOCAL = Path(args.binary)
REMOTE = "/userdata/xtapp/xt_gateway"
EXPECTED_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"


def fingerprint(key):
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")


def run(transport, command):
    channel = transport.open_session()
    channel.exec_command(command)
    output = bytearray()
    while not (channel.exit_status_ready() and not channel.recv_ready()):
        if channel.recv_ready():
            output.extend(channel.recv(65536))
    output.extend(channel.recv(65536))
    status = channel.recv_exit_status()
    channel.close()
    print(output.decode(errors="replace"), end="")
    if status:
        raise RuntimeError(f"command failed ({status}): {command}")


local_hash = hashlib.sha256(LOCAL.read_bytes()).hexdigest()
transport = paramiko.Transport(("203.0.113.179", 22))
transport.connect()
assert fingerprint(transport.get_remote_server_key()) == EXPECTED_FP, "fingerprint mismatch"
transport.auth_password("root", "root")
stamp = time.strftime("%Y%m%d-%H%M%S")
backup = f"{REMOTE}.before-query-churn-hotfix-{stamp}"
run(transport, f"cp -p {REMOTE} {backup}")
sftp = paramiko.SFTPClient.from_transport(transport)
sftp.put(str(LOCAL), REMOTE + ".new")
sftp.chmod(REMOTE + ".new", 0o755)
sftp.close()
run(transport, f"test \"$(sha256sum {REMOTE}.new | cut -d' ' -f1)\" = {local_hash}")
run(transport, "killall xt_gateway 2>/dev/null || true; "
               f"mv {REMOTE}.new {REMOTE}; rm -f /var/run/xt_gateway.pid; "
               "/etc/init.d/S98zzgateway start")
time.sleep(4)
run(transport, f"sha256sum {REMOTE}; pidof xt_gateway; tail -n 15 /userdata/xtapp/xt_gateway.log")
transport.close()
print(f"DEPLOYED hash={local_hash} backup={backup}")
