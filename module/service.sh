#!/system/bin/sh
# PenBridge - KernelSU / Magisk late_start service
MODDIR=${0%/*}
CONF="$MODDIR/config.txt"
LOG=/data/local/tmp/penbridge.log
FLAG=/data/adb/penbridge.disabled

[ -f "$FLAG" ] && exit 0
[ -f "$CONF" ] && . "$CONF"
ROLE=${ROLE:-solo}
PORT=${PORT:-47801}

case "$ROLE" in
  solo)
    set -- solo "${KEYBOARD:-Focus Pen}" "${STYLUS:-Xiaomi_Touch_Stylus}"
    [ -n "$CLONE_NAME" ] && set -- "$@" "$CLONE_NAME"
    ;;
  tx) set -- tx "${PEER:-127.0.0.1}" "$PORT" ;;
  rx) set -- rx "$PORT" "${DEVNAME:-sec_e-pen}" ;;
  *)  exit 0 ;;
esac

[ -f "$LOG" ] && [ "$(wc -c < "$LOG" 2>/dev/null || echo 0)" -gt 262144 ] && mv -f "$LOG" "$LOG.old"

(
  exec </dev/null
  while true; do
    [ -f "$FLAG" ] && break
    echo "--- $(date) start: penbridge $*"
    "$MODDIR/bin/penbridge" "$@"
    [ -f "$FLAG" ] && break
    sleep 5
  done
) >>"$LOG" 2>&1 &
