#!/usr/bin/env python3
"""Freeze the customer-visible Protobuf field numbers and scalar types."""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from google.protobuf.descriptor import FieldDescriptor
from pc import active_msgs_pb2 as pb


EXPECTED = {
    "Header": {"seq": (1, "uint32"), "stamp": (2, "sint64"), "scaler": (3, "sint64")},
    "ImageMsg": {"header": (1, "Header"), "format": (2, "ImageFormat"),
                 "width": (3, "uint32"), "height": (4, "uint32"), "data": (5, "bytes")},
    "ImageMsgArray": {"array": (1, "ImageMsg")},
    "LidarPoint": {"x": (1, "sint64"), "y": (2, "sint64"), "z": (3, "sint64")},
    "LidarPointMsg": {"header": (1, "Header"), "points": (2, "LidarPoint")},
    "LidarPointMsgArray": {"array": (1, "LidarPointMsg")},
    "SettingRequest": {"image_format": (5, "sint32")},
}
SCALAR_TYPES = {
    "uint32": FieldDescriptor.TYPE_UINT32,
    "sint32": FieldDescriptor.TYPE_SINT32,
    "sint64": FieldDescriptor.TYPE_SINT64,
    "bytes": FieldDescriptor.TYPE_BYTES,
}


def source_fields(text: str, message: str) -> dict[str, tuple[int, str]]:
    match = re.search(r"\bmessage\s+" + re.escape(message) + r"\s*\{([^}]*)\}", text, re.S)
    if not match:
        raise ValueError(f"missing message {message}")
    body = re.sub(r"//[^\n]*", "", match.group(1))
    return {name: (int(number), kind) for kind, name, number in re.findall(
        r"\b(?:repeated\s+)?(\w+)\s+(\w+)\s*=\s*(\d+)\s*;", body)}


def field_type(field) -> str:
    if field.type == FieldDescriptor.TYPE_MESSAGE or field.type == FieldDescriptor.TYPE_ENUM:
        return field.message_type.name if field.message_type else field.enum_type.name
    for name, value in SCALAR_TYPES.items():
        if field.type == value:
            return name
    return f"type={field.type}"


def read_varint(data: bytes, at: int) -> tuple[int, int]:
    value = 0
    for shift in range(0, 70, 7):
        if at >= len(data):
            raise ValueError("truncated varint")
        byte = data[at]
        at += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, at
    raise ValueError("varint too long")


def wire_fields(data: bytes) -> list[tuple[int, int, bytes | int]]:
    fields = []
    at = 0
    while at < len(data):
        tag, at = read_varint(data, at)
        number, wire = tag >> 3, tag & 7
        if not number:
            raise ValueError("field number zero")
        if wire == 0:
            value, at = read_varint(data, at)
        elif wire == 2:
            size, at = read_varint(data, at)
            if at + size > len(data):
                raise ValueError("truncated length-delimited field")
            value = data[at:at + size]
            at += size
        else:
            raise ValueError(f"unexpected wire type {wire}")
        fields.append((number, wire, value))
    return fields


def check_wire_sample(raw: bytes, stream: str) -> list[str]:
    errors = []
    outer = wire_fields(raw)
    if not outer or any(number != 1 or wire != 2 for number, wire, _ in outer):
        return [f"{stream}: outer array is not repeated field 1, wire 2"]
    for _, _, item in outer:
        inner = wire_fields(item)
        expected = {1: 2, 2: 0 if stream == "image" else 2}
        if stream == "image":
            expected.update({3: 0, 4: 0, 5: 2})
        for number, wire, value in inner:
            if number in expected and wire != expected[number]:
                errors.append(f"{stream}: field {number} wire {wire} != {expected[number]}")
            if number == 1:
                for header_number, header_wire, _ in wire_fields(value):
                    if header_number in (1, 2, 3) and header_wire != 0:
                        errors.append(f"{stream}: Header.{header_number} wire {header_wire} != 0")
            if stream == "pointcloud" and number == 2:
                for point_number, point_wire, _ in wire_fields(value):
                    if point_number in (1, 2, 3) and point_wire != 0:
                        errors.append(f"pointcloud: LidarPoint.{point_number} wire {point_wire} != 0")
        for required in (1, 2, 3, 4, 5) if stream == "image" else (1, 2):
            if not any(number == required for number, _, _ in inner):
                errors.append(f"{stream}: missing field {required} in sample")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--proto", type=Path, default=ROOT / "protocol/active_msgs.proto")
    parser.add_argument("--capture", type=Path, help="directory with raw/raw/{image,pointcloud}/000001.bin")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = args.proto.read_text(encoding="utf-8")
    errors = []
    checked = 0
    for message, fields in EXPECTED.items():
        actual_source = source_fields(source, message)
        descriptor = getattr(pb, message).DESCRIPTOR
        for name, expected in fields.items():
            checked += 1
            if actual_source.get(name) != expected:
                errors.append(f"proto {message}.{name}: {actual_source.get(name)} != {expected}")
            compiled = descriptor.fields_by_name.get(name)
            actual_compiled = None if compiled is None else (compiled.number, field_type(compiled))
            if actual_compiled != expected:
                errors.append(f"generated {message}.{name}: {actual_compiled} != {expected}")
    if pb.ImageFormatH264 != 3 or pb.ImageFormatJpeg != 2:
        errors.append("ImageMsg format enum changed")
    # This is the setting request's separate legacy value, not ImageMsg.format.
    if pb.SettingRequest(image_format=1).image_format != 1:
        errors.append("SettingRequest H264 value changed")
    if args.capture:
        for stream in ("image", "pointcloud"):
            sample = args.capture / "raw" / "raw" / stream / "000001.bin"
            if not sample.is_file():
                errors.append(f"missing wire sample {sample}")
            else:
                try:
                    errors.extend(check_wire_sample(sample.read_bytes(), stream))
                except ValueError as exc:
                    errors.append(f"{stream} wire parse: {exc}")
    result = {"status": "PASS" if not errors else "FAIL", "checked_fields": checked,
              "image_msg_h264": 3, "setting_request_h264": 1, "errors": errors}
    rendered = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
