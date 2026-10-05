#!/usr/bin/env python3
"""Query the board's NTP service and report clock offset and round-trip delay."""

import argparse
import json
import socket
import struct
import time

NTP_EPOCH = 2208988800


def encode(value):
    seconds = value + NTP_EPOCH
    whole = int(seconds)
    return struct.pack("!II", whole, int((seconds - whole) * (1 << 32)))


def decode(value):
    whole, fraction = struct.unpack("!II", value)
    return whole - NTP_EPOCH + fraction / (1 << 32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--samples", type=int, default=5)
    args = ap.parse_args()
    samples = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((args.bind, 0))
        sock.settimeout(2)
        for _ in range(args.samples):
            request = bytearray(48)
            request[0] = 0x23  # NTPv4 client
            t1 = time.time()
            request[40:48] = encode(t1)
            sock.sendto(request, (args.host, 123))
            reply, _ = sock.recvfrom(512)
            t4 = time.time()
            if len(reply) < 48 or reply[0] & 7 != 4:
                raise RuntimeError("invalid NTP server reply")
            t2, t3 = decode(reply[32:40]), decode(reply[40:48])
            samples.append({"stratum": reply[1],
                            "offset_ms": round(((t2 - t1) + (t3 - t4)) * 500, 3),
                            "roundtrip_ms": round(((t4 - t1) - (t3 - t2)) * 1000, 3)})
            time.sleep(0.25)
    print(json.dumps({"server": args.host, "source": args.bind, "samples": samples}, indent=2))


if __name__ == "__main__":
    main()
