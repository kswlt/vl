#!/usr/bin/env bash
# Capture only the device Zenoh TCP endpoint. Run on a Linux PC with tcpdump.
set -euo pipefail
if [ "$#" -lt 4 ]; then
  echo "usage: $0 INTERFACE DEVICE_IP DURATION_SECONDS OUTPUT_DIR" >&2
  exit 2
fi
iface=$1
device_ip=$2
duration=$3
out=$4
mkdir -p "$out/pcap" "$out/logs"
pcap="$out/pcap/acceptance.pcap"
meta="$out/pcap/metadata.json"
started=$(date -u +%Y-%m-%dT%H:%M:%SZ)
timeout "$duration" tcpdump -i "$iface" -nn -s 0 -w "$pcap" "host $device_ip and tcp port 7447" &
pid=$!
wait "$pid" || test $? -eq 124
ended=$(date -u +%Y-%m-%dT%H:%M:%SZ)
printf '{\n  "interface": "%s",\n  "device_ip": "%s",\n  "filter": "host %s and tcp port 7447",\n  "started_utc": "%s",\n  "ended_utc": "%s"\n}\n' "$iface" "$device_ip" "$device_ip" "$started" "$ended" > "$meta"
