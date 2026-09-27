#!/bin/sh
set -eu
pid_before=$(pidof xt_camera)
chain=XT_CAM_AUDIT
created=0
jumped=0
cleanup() {
    if [ "$jumped" = 1 ]; then iptables -D OUTPUT -j "$chain"; jumped=0; fi
    if [ "$created" = 1 ]; then iptables -F "$chain"; iptables -X "$chain"; created=0; fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
# Creating a fresh private chain fails if it already exists; never alter an existing chain.
iptables -N "$chain"
created=1
iptables -A "$chain" -d 192.0.2.123 -p tcp --dport 80 -j DROP
iptables -I OUTPUT 1 -j "$chain"
jumped=1
echo "camera HTTP blocked pid=$pid_before uptime=$(cat /proc/uptime)"
sleep 5
cleanup
echo "camera HTTP restored uptime=$(cat /proc/uptime)"
sleep 10
echo "camera after pid=$(pidof xt_camera) uptime=$(cat /proc/uptime)"
test "$pid_before" = "$(pidof xt_camera)"
tail -15 /userdata/xtapp/camera.log
