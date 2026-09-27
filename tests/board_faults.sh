#!/bin/sh
# Controlled interruption of the real sensor streams; never reboot the board.
set -eu
APP=/userdata/xtapp
case "${1:-}" in ""|--radar-only) ;; *) exit 2 ;; esac
if ! ip route get 192.0.2.101 | grep -q 'dev eth0'; then
    echo 'This test requires the legacy dedicated eth0 radar link; shared-switch topology must not down eth1.' >&2
    exit 2
fi
radar_before=$(pidof xt_radar)
camera_before=$(pidof xt_camera)
echo "before radar=$radar_before camera=$camera_before uptime=$(cat /proc/uptime)"
link_down=0
restore_legacy_default=0
if ip route show | grep -q '^default via 198.51.100.200 dev eth0'; then restore_legacy_default=1; fi
cleanup() {
    if [ "$link_down" = 1 ]; then ip link set eth0 up; link_down=0; fi
    if [ "$restore_legacy_default" = 1 ]; then ip route replace default via 198.51.100.200 dev eth0; fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
ip link set eth0 down
link_down=1
echo "radar eth0 down uptime=$(cat /proc/uptime)"
sleep 6
ip link set eth0 up
link_down=0
echo "radar eth0 up uptime=$(cat /proc/uptime)"
sleep 20
echo "after link recovery radar=$(pidof xt_radar) camera=$(pidof xt_camera) uptime=$(cat /proc/uptime)"
tail -12 "$APP/xt_radar.log"
if [ "${1:-}" != --radar-only ]; then sh "$(dirname "$0")/camera_http_fault.sh"; fi
echo "after radar=$(pidof xt_radar) camera=$(pidof xt_camera) uptime=$(cat /proc/uptime)"
test "$radar_before" = "$(pidof xt_radar)"
test "$camera_before" = "$(pidof xt_camera)"
tail -15 "$APP/camera.log"
tail -12 "$APP/xt_radar.log"
ip route get 192.0.2.101
ip route get 192.0.2.101 | grep -q 'dev eth0'
