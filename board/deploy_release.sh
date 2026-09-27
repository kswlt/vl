#!/bin/sh
# Run on the board after the container build and unit tests have passed.
set -eu
APP=/userdata/xtapp
STAGE="$APP/release"
BACKUP="$APP/backup-before-jpeg-config-recovery"
if [ -e "$BACKUP" ]; then
    if [ "${1:-}" != "--reuse-backup" ]; then
        echo "Backup exists; use --reuse-backup only when continuing this deployment." >&2
        exit 1
    fi
else
mkdir "$BACKUP"
for name in xt_camera xt_radar xt_gateway xt_camera.cpp xt_radar.cpp config.json lidar_intrinsics.json; do
    if [ -f "$APP/$name" ]; then cp -p "$APP/$name" "$BACKUP/$name"; fi
done
cp -p /mnt/system/factory-data/Legacy.yaml "$BACKUP/Legacy.yaml"
cp -p /etc/init.d/S99xtcamera /etc/init.d/S99xtradar /etc/init.d/S98zzgateway "$BACKUP/" 2>/dev/null || true
fi
docker cp xtbuilder:/tmp/xtbuild/board/xt_camera "$STAGE/xt_camera.new"
docker cp xtbuilder:/tmp/xtbuild/board/xt_radar "$STAGE/xt_radar.new"
docker cp xtbuilder:/tmp/xtbuild/board/xt_gateway "$STAGE/xt_gateway.new"
chmod 755 "$STAGE/xt_camera.new" "$STAGE/xt_radar.new" "$STAGE/xt_gateway.new"
sha256sum "$STAGE/xt_camera.new" "$STAGE/xt_radar.new" "$STAGE/xt_gateway.new"
# Exact process names avoid the legacy camera script's pkill -f matching this shell.
killall xt_camera xt_radar xt_gateway 2>/dev/null || true
sleep 2
mv "$STAGE/xt_camera.new" "$APP/xt_camera"
mv "$STAGE/xt_radar.new" "$APP/xt_radar"
mv "$STAGE/xt_gateway.new" "$APP/xt_gateway"
cp "$STAGE/board/src/"* "$APP/"
for script in S98zzgateway S99xtradar S99xtcamera; do
    cp "$STAGE/board/init/$script" "/etc/init.d/$script"
    chmod 755 "/etc/init.d/$script"
done
mkdir -p "$APP/web"
cp "$STAGE/board/web/index.html" "$APP/web/index.html"
cp "$STAGE/board/init/S50xtmanagement" /etc/init.d/S50xtmanagement
chmod 755 /etc/init.d/S50xtmanagement
echo "SSH_ENABLED='1'" > "$APP/management-ssh.conf"
/etc/init.d/S50xtmanagement ssh
rm -f /var/run/xt_radar.pid /var/run/xt_camera.pid /var/run/xt_gateway.pid
/etc/init.d/S98zzgateway start
sleep 2
/etc/init.d/S99xtradar start
/etc/init.d/S99xtcamera start
sleep 5
echo 'DEPLOYED; factory file must remain unchanged:'
sha256sum /mnt/system/factory-data/Legacy.yaml "$BACKUP/Legacy.yaml"
pidof xt_camera xt_radar xt_gateway
tail -15 "$APP/camera.log"
tail -20 "$APP/xt_radar.log"
