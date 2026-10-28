#!/system/bin/sh
# PenBridge - KernelSU / Magisk late_start service
MODDIR=${0%/*}
LOG=/data/local/tmp/penbridge.log
FLAG=/data/adb/penbridge.disabled

[ -f "$FLAG" ] && exit 0
[ -f "$LOG" ] && [ "$(wc -c < "$LOG" 2>/dev/null || echo 0)" -gt 262144 ] && mv -f "$LOG" "$LOG.old"

(
  exec </dev/null
  while true; do
    [ -f "$FLAG" ] && break
    if [ -z "$(pidof penbridge)" ]; then
      echo "--- $(date) start"
      "$MODDIR/bin/penbridge"
      [ -f "$FLAG" ] && break
    fi
    sleep 5
  done
) >>"$LOG" 2>&1 &
