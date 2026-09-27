#!/usr/bin/env python3
"""Move the RK3562 camera/radar host routes to eth0 after cable changes."""

from __future__ import annotations

import base64
import hashlib
import json
import posixpath
import stat
import time

import paramiko


HOST = "203.0.113.179"
PORT = 22
USER = "root"
PASSWORD = "root"
EXPECTED_FP = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"
CONFIG = "/userdata/xtapp/config.json"
NET_INIT = "/etc/init.d/S98xtnet"


def fingerprint(key: paramiko.PKey) -> str:
    digest = hashlib.sha256(key.asbytes()).digest()
    return "SHA256:" + base64.b64encode(digest).decode().rstrip("=")


def read_file(sftp: paramiko.SFTPClient, path: str) -> bytes:
    with sftp.open(path, "rb") as handle:
        return handle.read()


def write_atomic(sftp: paramiko.SFTPClient, path: str, data: bytes, mode: int) -> None:
    temporary = path + ".codex-new"
    with sftp.open(temporary, "wb") as handle:
        handle.write(data)
        handle.flush()
    sftp.chmod(temporary, mode)
    try:
        sftp.stat(path)
    except OSError:
        pass
    else:
        sftp.remove(path)
    sftp.rename(temporary, path)


def main() -> None:
    transport = paramiko.Transport((HOST, PORT))
    transport.banner_timeout = 30
    transport.connect(username=USER, password=PASSWORD)
    key = transport.get_remote_server_key()
    actual_fp = fingerprint(key)
    if key.get_name() != "ssh-ed25519" or actual_fp != EXPECTED_FP:
        transport.close()
        raise SystemExit(f"host key mismatch: {key.get_name()} {actual_fp}")

    sftp = paramiko.SFTPClient.from_transport(transport)
    config_raw = read_file(sftp, CONFIG)
    init_raw = read_file(sftp, NET_INIT)

    config = json.loads(config_raw.decode("utf-8"))
    radar_network = config.setdefault("radar_network", {})
    before = dict(radar_network)
    radar_network["interface"] = "eth0"
    radar_network["source_address"] = "192.0.2.179"
    config_new = (json.dumps(config, ensure_ascii=False, indent=2) + "\n").encode("utf-8")

    init_text = init_raw.decode("utf-8")
    camera_route = (
        "    # Camera shares the sensor switch on eth0.\n"
        "    ip route replace 192.0.2.123/32 dev eth0 src 192.0.2.179 2>/dev/null\n"
    )
    if "192.0.2.123/32 dev eth0" not in init_text:
        marker = "    ip route replace 192.0.2.101/32 dev eth0 src 192.0.2.179 2>/dev/null\n"
        if marker not in init_text:
            raise SystemExit("expected radar route marker not found; refusing to modify init script")
        init_text = init_text.replace(marker, marker + camera_route, 1)
    init_new = init_text.encode("utf-8")

    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup_dir = f"/userdata/xtapp/backup-sensor-port-{stamp}"
    sftp.mkdir(backup_dir)
    write_atomic(sftp, posixpath.join(backup_dir, "config.json"), config_raw, 0o644)
    write_atomic(sftp, posixpath.join(backup_dir, "S98xtnet"), init_raw, 0o755)

    write_atomic(sftp, CONFIG, config_new, stat.S_IMODE(sftp.stat(CONFIG).st_mode))
    write_atomic(sftp, NET_INIT, init_new, 0o755)
    sftp.close()
    transport.close()

    print(json.dumps({"backup": backup_dir, "radar_network_before": before,
                      "radar_network_after": radar_network}, ensure_ascii=False))


if __name__ == "__main__":
    main()
