# -*- coding: utf-8 -*-
"""Raw protobuf wire structure comparison: decode tag tree without .proto to spot unknown fields."""
import json
from pathlib import Path

BASE = Path(__file__).resolve().parents[1] / "evidence/dual-compare"


def read_varint(b, i):
    shift = 0
    val = 0
    while True:
        c = b[i]
        i += 1
        val |= (c & 0x7F) << shift
        if not c & 0x80:
            return val, i
        shift += 7


def structure(buf, depth=0, max_elems=3):
    """Return list of (field, wiretype, kind) for a message buffer."""
    i = 0
    out = []
    elem_counts = {}
    while i < len(buf):
        tag, i = read_varint(buf, i)
        field, wt = tag >> 3, tag & 7
        key = (field, wt)
        elem_counts[key] = elem_counts.get(key, 0) + 1
        idx = elem_counts[key]
        if wt == 0:
            _, i = read_varint(buf, i)
            kind = "varint"
        elif wt == 1:
            kind = "64bit"; i += 8
        elif wt == 2:
            ln, i = read_varint(buf, i)
            payload = buf[i:i + ln]
            kind = f"len{ln}"
            # try to interpret as embedded message: decode and check it ends exactly
            sub = None
            try:
                sub = structure(payload, depth + 1)
            except Exception:
                sub = None
            if sub is not None and depth < 4 and idx <= max_elems:
                kind = {"len": ln, "sub": sub}
            i += ln
        elif wt == 5:
            kind = "32bit"; i += 4
        else:
            kind = f"UNKNOWN_WT{wt}"; break
        if idx <= max_elems:
            out.append({"f": field, "wt": wt, "k": kind})
    return out


def wire_sig(buf):
    """Compact signature: ordered unique (field,wt) tags at top and one level into arrays/elements."""
    top = structure(buf, max_elems=1)
    return top


report = {}
for topic in ("image", "pointcloud", "imu"):
    report[topic] = {}
    for dev in ("rk3562", "legacy"):
        d = BASE / dev / "raw" / topic
        files = sorted(d.glob("*.bin"))
        if not files:
            report[topic][dev] = None
            continue
        report[topic][dev] = {"file": files[0].name, "wire": wire_sig(files[0].read_bytes())}

same = {}
for topic, devs in report.items():
    if devs.get("rk3562") and devs.get("legacy"):
        same[topic] = json.dumps(devs["rk3562"]["wire"], sort_keys=True) == json.dumps(devs["legacy"]["wire"], sort_keys=True)

print(json.dumps({"identical_wire_tree": same, "report": report}, indent=2, ensure_ascii=False))
(BASE / "wire_structure.json").write_text(json.dumps({"identical_wire_tree": same, "report": report}, indent=2, ensure_ascii=False), encoding="utf-8")
