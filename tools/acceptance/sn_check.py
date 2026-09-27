#!/usr/bin/env python3
"""Validate stable, unique SN to endpoint assignments for one or more RK boards."""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
import zenoh
from pc import active_msgs_pb2 as pb


def query(endpoint: str, sn: str) -> dict:
    cfg = zenoh.Config()
    cfg.insert_json5("mode", '"client"')
    cfg.insert_json5("connect/endpoints", json.dumps([endpoint]))
    cfg.insert_json5("scouting/multicast/enabled", "false")
    with zenoh.open(cfg) as session:
        replies = [r.ok.payload.to_bytes() for r in session.get(
            f"active/{sn}/config_file", payload=b"", timeout=5) if r.ok]
    if len(replies) != 1:
        raise RuntimeError(f"{endpoint}: config_file replies={len(replies)}")
    for msg in pb.FileMsgArray.FromString(replies[0]).array:
        if msg.path == "config.json":
            return json.loads(msg.data)
    raise RuntimeError(f"{endpoint}: config.json absent")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--device", action="append", required=True, metavar="SN@tcp/IP:7447",
                    help="repeat for every board in the deployment")
    ap.add_argument("--inventory", type=Path, help="write identity inventory for comparison after reboot")
    args = ap.parse_args()
    errors = []
    identities = []
    for pair in args.device:
        try:
            sn, endpoint = pair.split("@", 1)
            if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", sn):
                raise ValueError("invalid SN characters")
            if not endpoint.startswith("tcp/") or not endpoint.split("#", 1)[0].endswith(":7447"):
                raise ValueError("expected tcp/IP:7447 endpoint")
            config = query(endpoint, sn)
            compat = config.get("legacy_compatibility", {})
            configured = compat.get("legacy_sn") if compat.get("legacy_topics_only") else sn
            if configured != sn:
                errors.append(f"{endpoint}: topic SN {sn} differs from configured {configured}")
            identities.append({"sn": sn, "endpoint": endpoint, "configured_sn": configured})
        except Exception as exc:
            errors.append(f"{pair}: {exc}")
    if len({item["sn"] for item in identities}) != len(identities):
        errors.append("duplicate SN across endpoints")
    if len({item["endpoint"] for item in identities}) != len(identities):
        errors.append("one endpoint assigned multiple SN values")
    if args.inventory:
        if args.inventory.exists():
            prior = json.loads(args.inventory.read_text(encoding="utf-8"))
            if prior.get("identities") != identities:
                errors.append("identity changed since prior inventory (or device order differs)")
        elif not errors:
            args.inventory.parent.mkdir(parents=True, exist_ok=True)
            args.inventory.write_text(json.dumps({"identities": identities}, indent=2) + "\n", encoding="utf-8")
    result = {"status": "PASS" if not errors else "FAIL", "identities": identities,
              "multi_device_verified": len(identities) > 1, "errors": errors}
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
