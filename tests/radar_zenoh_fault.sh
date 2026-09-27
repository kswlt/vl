#!/bin/sh
# Deliberately block only radar Zenoh egress, never sensor UDP or camera HTTP/Zenoh.
set -eu
CHAIN=XT_RADAR_PUB_AUDIT
cleanup() {
    iptables -D OUTPUT -d 192.0.2.200 -p tcp --sport 7447 -j "$CHAIN" 2>/dev/null || true
    iptables -F "$CHAIN" 2>/dev/null || true
    iptables -X "$CHAIN" 2>/dev/null || true
}
iptables -N "$CHAIN"
trap cleanup EXIT HUP INT TERM
echo 'BEGIN radar publish backpressure; uptime and PIDs:'
cat /proc/uptime
pidof xt_radar xt_camera
tail -6 /userdata/xtapp/xt_radar.log
iptables -A "$CHAIN" -j DROP
iptables -I OUTPUT 1 -d 192.0.2.200 -p tcp --sport 7447 -j "$CHAIN"
sleep 12
echo 'DURING DROP:'
cat /proc/uptime
tail -10 /userdata/xtapp/xt_radar.log
iptables -L "$CHAIN" -n -v
cleanup
echo 'RESTORED:'
cat /proc/uptime
sleep 20
pidof xt_radar xt_camera
tail -20 /userdata/xtapp/xt_radar.log
