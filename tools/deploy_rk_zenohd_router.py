"""Install zenohd as RK's public 7447 endpoint and move xt_gateway behind it."""
import base64
import hashlib
import time
from pathlib import Path

import paramiko

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"


def fp(key):
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")


def run(t, command, required=True):
    ch = t.open_session()
    ch.exec_command(command)
    out, err = bytearray(), bytearray()
    while not (ch.exit_status_ready() and not ch.recv_ready() and not ch.recv_stderr_ready()):
        if ch.recv_ready():
            out.extend(ch.recv(65536))
        if ch.recv_stderr_ready():
            err.extend(ch.recv_stderr(65536))
    out.extend(ch.recv(65536)); err.extend(ch.recv_stderr(65536))
    status = ch.recv_exit_status(); ch.close()
    print(out.decode(errors="replace"), end="")
    print(err.decode(errors="replace"), end="")
    if required and status:
        raise RuntimeError(f"command failed ({status}): {command}")
    return status


t = paramiko.Transport(("203.0.113.179", 22)); t.connect()
assert fp(t.get_remote_server_key()) == EXPECTED_FP
t.auth_password("root", "root")
stamp = time.strftime("%Y%m%d-%H%M%S")
backup = f"/userdata/xtapp/backup-before-zenohd-{stamp}"
run(t, f"mkdir {backup} && cp -p /userdata/xtapp/config.json /userdata/xtapp/xt_gateway {backup}/")
sftp = paramiko.SFTPClient.from_transport(t)
sftp.put(str(ROOT / "config" / "board.single_endpoint.json"), "/userdata/xtapp/config.json.router-new")
sftp.put(str(ROOT / "board" / "init" / "S97zenohd"), "/etc/init.d/S97zenohd.new")
sftp.chmod("/etc/init.d/S97zenohd.new", 0o755)
sftp.close()
try:
    run(t, "killall xt_gateway 2>/dev/null || true; rm -f /var/run/xt_gateway.pid; "
           "mv /userdata/xtapp/zenohd.candidate /userdata/xtapp/zenohd; chmod 755 /userdata/xtapp/zenohd; "
           "mv /etc/init.d/S97zenohd.new /etc/init.d/S97zenohd; /etc/init.d/S97zenohd start; sleep 3; "
           "test -s /var/run/zenohd.pid; kill -0 $(cat /var/run/zenohd.pid)")
    run(t, "mv /userdata/xtapp/config.json.router-new /userdata/xtapp/config.json; "
           "/etc/init.d/S98zzgateway start; sleep 5; pidof zenohd xt_gateway; "
           "grep -a -E 'z_open ret=0|session ready|queryable.*ready' /userdata/xtapp/xt_gateway.log | tail -n 8; "
           "tail -n 12 /userdata/xtapp/zenohd.log")
except Exception:
    run(t, f"killall xt_gateway 2>/dev/null || true; /etc/init.d/S97zenohd stop 2>/dev/null || true; "
           f"cp -p {backup}/config.json /userdata/xtapp/config.json; rm -f /var/run/xt_gateway.pid; "
           "/etc/init.d/S98zzgateway start", required=False)
    t.close()
    raise
t.close()
print(f"DEPLOYED zenohd router; backup={backup}")
