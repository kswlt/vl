"""Read-only capture of the known RK board's deployable files.

This intentionally omits factory identity, calibration, credentials and mutable
network settings.  The second board must create those locally.
"""
import base64
import hashlib
import json
from pathlib import Path

import paramiko


HOST = "198.51.100.179"
FINGERPRINT = "SHA256:Oa6mKwXI0b7Ybt3BYNnI7LAeU1eWYHl6bgCQ85bvsuM"
DEST = Path(__file__).resolve().parents[1] / "output" / "rk3562_second_board_handoff" / "live_release"
APP_NAMES = ["xt_gateway", "xt_camera", "xt_radar", "vizd", "ffmpeg", "ffmpeg_fmp4", "zenohd", "web/index.html"]
INIT_NAMES = ["S48xtrtc", "S50xtmanagement", "S50xtntp", "S97xttime", "S97zenohd", "S98xtnet", "S98zzgateway", "S98zzvizd", "S99xtcamera", "S99xtcamtime", "S99xtradar"]


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    transport = paramiko.Transport((HOST, 22))
    transport.start_client(timeout=10)
    fingerprint = "SHA256:" + base64.b64encode(hashlib.sha256(transport.get_remote_server_key().asbytes()).digest()).decode().rstrip("=")
    if fingerprint != FINGERPRINT:
        raise RuntimeError(f"SSH host key mismatch: {fingerprint}")
    # Current board's existing credential, never copied into the handoff files.
    transport.auth_password(username="root", password="root")
    sftp = paramiko.SFTPClient.from_transport(transport)
    items = []
    for base, names, out in (("/userdata/xtapp", APP_NAMES, "app"), ("/etc/init.d", INIT_NAMES, "init")):
        for name in names:
            remote = f"{base}/{name}"
            local = DEST / out / name
            local.parent.mkdir(parents=True, exist_ok=True)
            sftp.get(remote, str(local))
            data = local.read_bytes()
            items.append({"path": f"{out}/{name}", "source": remote, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
            print(f"captured {out}/{name} ({len(data)} bytes)")
    (DEST / "SOURCE_MANIFEST.json").write_text(json.dumps({"host": HOST, "ssh_fingerprint": fingerprint, "files": items}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    sftp.close()
    transport.close()


if __name__ == "__main__":
    main()
