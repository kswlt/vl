"""Read-only settings and malformed-input checks. Never requests reboot or changes settings."""
import json
from pathlib import Path
from google.protobuf.json_format import MessageToDict
import zenoh
from pc import active_msgs_pb2 as pb

cfg = json.loads(Path("config/pc.json").read_text(encoding="utf-8"))
c = zenoh.Config()
c.insert_json5("mode", '"client"')
c.insert_json5("connect/endpoints", json.dumps([cfg["pointcloud_endpoint"]]))
c.insert_json5("scouting/multicast/enabled", "false")
report = {"note": "No positive setting fields; no reboot. Replies consume the board's shared pointcloud seq.", "requests": {}}
with zenoh.open(c) as session:
    for name, payload in [("read_settings", pb.SettingRequest(header=pb.Header(seq=1)).SerializeToString()),
                           ("malformed_protobuf", b"\x80")]:
        results = []
        for reply in session.get(f"active/{cfg['sn']}/cmd/setting", payload=payload, timeout=3):
            if reply.ok:
                msg = pb.SettingResponse.FromString(reply.ok.payload.to_bytes())
                results.append({"code":msg.error.code, "message":MessageToDict(msg, preserving_proto_field_name=True)})
            else:
                results.append({"transport_error":str(reply.err)})
        report["requests"][name] = results
Path("evidence/commands.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
print(json.dumps(report,indent=2))
