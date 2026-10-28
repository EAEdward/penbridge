#!/system/bin/sh
# PenBridge - KernelSU late_start entry.
# One launch, no supervision: the daemon owns its own reconnect loop and never exits
# on its own. A second launch is blocked by its flock.
MODDIR=${0%/*}
LOG=/data/local/tmp/penbridge.log
FLAG=/data/adb/penbridge.disabled

[ -f "$FLAG" ] && exit 0
[ -f "$LOG" ] && [ "$(wc -c < "$LOG" 2>/dev/null || echo 0)" -gt 262144 ] && mv -f "$LOG" "$LOG.old"

chmod 755 "$MODDIR/bin/penbridge" 2>/dev/null
echo "--- $(date) boot launch" >>"$LOG"
setsid "$MODDIR/bin/penbridge" >>"$LOG" 2>&1 </dev/null &
