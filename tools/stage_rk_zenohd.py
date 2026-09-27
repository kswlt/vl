"""Stage the official zenohd binary and inspect it on the verified RK board."""
import base64
import hashlib
import os
from pathlib import Path

import paramiko

LOCAL = Path(os.environ["TEMP"]) / "zenoh-1.10.0-aarch64-gnu" / "zenohd"
ARCHIVE = Path(os.environ["TEMP"]) / "zenoh-1.10.0-aarch64-gnu.zip"
EXPECTED_SHA256 = "1176a118ac609eb5d682ce5ab5a845bcba510789e4ed9c75e08f24ed6aa0908e"
EXPECTED_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"


def fp(key):
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")


assert hashlib.sha256(ARCHIVE.read_bytes()).hexdigest() == EXPECTED_SHA256
t = paramiko.Transport(("203.0.113.179", 22))
t.connect()
assert fp(t.get_remote_server_key()) == EXPECTED_FP
t.auth_password("root", "root")
sftp = paramiko.SFTPClient.from_transport(t)
sftp.put(str(LOCAL), "/userdata/xtapp/zenohd.candidate")
sftp.chmod("/userdata/xtapp/zenohd.candidate", 0o755)
sftp.close()
for command in ("/userdata/xtapp/zenohd.candidate --version",
                "/userdata/xtapp/zenohd.candidate --help | head -n 35"):
    channel = t.open_session()
    channel.exec_command(command)
    data = bytearray()
    while not (channel.exit_status_ready() and not channel.recv_ready()):
        if channel.recv_ready():
            data.extend(channel.recv(65536))
    data.extend(channel.recv(65536))
    print(data.decode(errors="replace"))
    assert channel.recv_exit_status() == 0
    channel.close()
t.close()
