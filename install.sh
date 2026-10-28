#!/bin/sh
# One-shot install: sh install.sh [serial]
set -e
SER="${1:-}"
ADB="adb ${SER:+-s $SER}"
HERE=$(cd "$(dirname "$0")" && pwd)

$ADB shell 'rm -rf /data/local/tmp/penbridge-stage; mkdir -p /data/local/tmp/penbridge-stage/bin'
$ADB push "$HERE/module/bin/penbridge" /data/local/tmp/penbridge-stage/bin/penbridge >/dev/null
for f in module.prop service.sh action.sh; do
  $ADB push "$HERE/module/$f" /data/local/tmp/penbridge-stage/$f >/dev/null
done
$ADB shell "su -c '
  pkill -9 -x penbridge || true
  rm -f /data/adb/penbridge.lock
  mkdir -p /data/adb/modules/penbridge/bin
  cp -f /data/local/tmp/penbridge-stage/bin/penbridge /data/adb/modules/penbridge/bin/
  cp -f /data/local/tmp/penbridge-stage/module.prop /data/local/tmp/penbridge-stage/service.sh         /data/local/tmp/penbridge-stage/action.sh /data/adb/modules/penbridge/
  chmod 755 /data/adb/modules/penbridge /data/adb/modules/penbridge/bin             /data/adb/modules/penbridge/bin/penbridge /data/adb/modules/penbridge/service.sh             /data/adb/modules/penbridge/action.sh
  chown -R root:root /data/adb/modules/penbridge
  rm -f /data/adb/penbridge.disabled
  setsid sh /data/adb/modules/penbridge/service.sh </dev/null >/dev/null 2>&1 &
'"
echo "installed; log: /data/local/tmp/penbridge.log"
