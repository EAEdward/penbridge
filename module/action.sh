#!/system/bin/sh
# KernelSU 管理器里的「操作」按钮：开关 PenBridge
FLAG=/data/adb/penbridge.disabled
MODDIR=${0%/*}
if [ -f "$FLAG" ]; then
  rm -f "$FLAG"
  setsid sh "$MODDIR/service.sh" >/dev/null 2>&1 &
  echo "PenBridge 已开启"
else
  touch "$FLAG"
  pkill -f penbridge
  echo "PenBridge 已暂停（笔恢复成普通无按键笔）"
fi
