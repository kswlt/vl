#!/bin/sh
# Runs only after the Windows deploy tool has uploaded and verified a unique stage.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP=/userdata/xtapp
INIT=/etc/init.d
MODE=${1:-check}
case "$MODE" in check|apply|rollback) ;; *) echo 'usage: install.sh check|apply|rollback [backup-dir]';exit 2;; esac
[ "$(id -u)" = 0 ] || { echo 'root required';exit 1; }
services='S99xtcamera S99xtradar S98zzgateway S97zenohd'
stop_services() { for s in $services; do [ ! -x "$INIT/$s" ] || "$INIT/$s" stop || true; done; }
start_services() { for s in S97zenohd S98zzgateway S99xtradar S99xtcamera; do "$INIT/$s" start; done; }
if [ "$MODE" = rollback ]; then
 B=${2:?backup directory required}
 case "$B" in /userdata/xtapp/backup-delivery-*) ;; *) echo 'invalid backup path';exit 1;; esac
 [ -r "$B/managed_paths" ] || { echo 'backup inventory missing';exit 1; }
 stop_services
 while IFS= read -r rel; do
  case "$rel" in app/*) target="$APP/${rel#app/}";; init/*) target="$INIT/${rel#init/}";; *) exit 1;; esac
  if [ -f "$B/$rel" ]; then cp -p "$B/$rel" "$target"; else rm -f "$target"; fi
 done < "$B/managed_paths"
 # A fresh board may not have contained an application before installation.
 for s in S97zenohd S98zzgateway S99xtradar S99xtcamera; do [ ! -x "$INIT/$s" ] || "$INIT/$s" start; done
 echo "ROLLED_BACK=$B";exit 0
fi
[ "$(uname -m)" = aarch64 ] || { echo 'requires aarch64 Linux';exit 1; }
for c in tar sha256sum awk ip df; do command -v "$c" >/dev/null || exit 1; done
(cd "$HERE" && sha256sum -c SHA256SUMS)
[ -d "$INIT" ] || exit 1
[ -x /usr/bin/gst-launch-1.0 ] || { echo 'Rockchip GStreamer runtime missing';exit 1; }
command -v gst-inspect-1.0 >/dev/null || { echo 'gst-inspect-1.0 missing';exit 1; }
for plugin in rtspsrc rtph264depay h264parse mppvideodec mppjpegenc; do
 gst-inspect-1.0 "$plugin" >/dev/null 2>&1 || { echo "GStreamer plugin missing: $plugin";exit 1; }
done
available=$(df -Pk /userdata | awk 'NR==2{print $4}')
[ "${available:-0}" -ge 131072 ] || { echo 'at least 128 MiB free on /userdata required';exit 1; }
for n in xt_camera xt_radar xt_gateway; do [ -f "$HERE/app/$n" ] || exit 1; done
if [ ! -x "$APP/zenohd" ] && [ ! -x /usr/bin/zenohd ]; then
 echo 'zenohd prerequisite missing. Install compatible official ARM64 zenohd separately first.';exit 1
fi
[ -f "$HERE/profile/config.json" ] && [ -f "$HERE/profile/camera.env" ] && [ -f "$HERE/profile/device_identity.yaml" ] || exit 1
if [ -f "$APP/config.json" ] && [ ! -f "$HERE/profile/ALLOW_CONFIG_REPLACE" ]; then
 echo 'existing config preserved: rerun with --configure to explicitly replace config after backup';exit 1
fi
if [ -f "$APP/calibration.json" ]; then
 echo 'existing calibration.json present: validate axis/version compatibility and remove or archive it before installing this trial axis version';exit 1
fi
echo 'PREFLIGHT_OK (does not prove sensor data or calibration acceptance)'
[ "$MODE" = apply ] || exit 0
STAMP=$(date +%Y%m%d-%H%M%S)
B="$APP/backup-delivery-$STAMP-$$"
umask 077
mkdir -p "$B/app" "$B/init"
for n in xt_camera xt_radar xt_gateway config.json camera.env runtime.env device_identity.yaml; do
 echo "app/$n" >> "$B/managed_paths"
 [ ! -f "$APP/$n" ] || cp -p "$APP/$n" "$B/app/$n"
done
for s in S97zenohd S98zzgateway S99xtradar S99xtcamera; do
 echo "init/$s" >> "$B/managed_paths"
 [ ! -f "$INIT/$s" ] || cp -p "$INIT/$s" "$B/init/$s"
done
cp "$HERE/install.sh" "$B/rollback.sh";chmod 700 "$B/rollback.sh"
echo "BACKUP=$B"
failed() { trap - EXIT; echo 'installation failed; attempting rollback'; "$B/rollback.sh" rollback "$B"; exit 1; }
trap 'failed' EXIT
stop_services
for n in xt_camera xt_radar xt_gateway; do cp "$HERE/app/$n" "$APP/$n.new";chmod 755 "$APP/$n.new";mv "$APP/$n.new" "$APP/$n";done
for n in config.json camera.env runtime.env device_identity.yaml; do cp "$HERE/profile/$n" "$APP/$n.new";chmod 600 "$APP/$n.new";mv "$APP/$n.new" "$APP/$n";done
for s in S97zenohd S98zzgateway S99xtradar S99xtcamera;do cp "$HERE/init/$s" "$INIT/$s.new";chmod 755 "$INIT/$s.new";mv "$INIT/$s.new" "$INIT/$s";done
start_services
sleep 5
for n in xt_gateway xt_radar xt_camera zenohd; do pidof "$n" >/dev/null || { echo "process missing: $n";exit 1; };done
for n in xt_camera xt_radar xt_gateway; do
 expected=$(sha256sum "$HERE/app/$n" | awk '{print $1}')
 actual=$(sha256sum "$APP/$n" | awk '{print $1}')
 [ "$actual" = "$expected" ] || exit 1
done
trap - EXIT
echo 'INSTALLED_WITH_HASH_AND_PROCESS_CHECKS (run independent dual-stream acceptance next)'
echo "Rollback: sh $B/rollback.sh rollback $B"
