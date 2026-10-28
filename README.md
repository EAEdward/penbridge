# PenBridge

把小米焦点触控笔的「轻捏」从**点一下切一次工具**改成**按住切橡皮、松手切回画笔**。

系统原本：捏一下 = 切一次。改完：按下 = 切一次（→ 橡皮）
## 原理

```
Xiaomi Focus Pen Pro Keyboard (evdev)
      │  EVIOCGRAB 独占，系统看不到原始轻捏
      ▼
  penbridge  ── 边沿判定：只认 DOWN(repeat=0) / UP
      │
      ▼
虚拟键盘 (uinput, 仅透传其它键 + 合成 KEY_F19 点按对)
      │
      ▼
系统照常执行一次「官方轻捏」→ 自己切工具
```

只抢占「轻捏」这一个键，其余按键（笔身双击、上下滑）原样转发，所以官方功能不受影响。

## 特性

- 断连/进程退出时兜底切回
- 只吞 `KEY_F19`，双击与滑动照常
- 笔休眠断连后自动重扫、重新独占
- 36KB 静态二进制，单进程，空闲 0% CPU
- 关掉即完全恢复原厂

## 安装

```sh
# 作为 KernelSU / Magisk 模块（推荐）
adb push module /data/local/tmp/penbridge
adb shell su -c 'cp -r /data/local/tmp/penbridge /data/adb/modules/penbridge && chmod -R 755 /data/adb/modules/penbridge'
# 重启，或先手动跑：
adb shell su -c 'setsid /data/adb/modules/penbridge/bin/penbridge </dev/null >>/data/local/tmp/penbridge.log 2>&1 &'
```

## 验证

```sh
adb shell getevent -lt /dev/input/eventXX   # 真实设备：按下/抬起各一次
adb shell dumpsys input | grep -i "focus pen"   # 应有 2 个（真实 + 虚拟）
```

预期日志：`down -> toggle` / `up -> toggle back`。

## 边界

只对「官方轻捏有动作」的场景生效（小米创作，或把轻捏设为切换画笔/橡皮的系统设置）。只认 `BTN_STYLUS` 的第三方笔记 app 不会因此获得橡皮。

## 构建

```sh
zig cc -target aarch64-linux-musl -O2 -static -fno-PIE -no-pie -s -o bin/penbridge src/replay.c
```

## 历史

`v0.2` 是早期方案：克隆整支电磁笔并注入 `BTN_STYLUS`，可让任何认按键的 app 获得橡皮，但要多一个设备、多一份事件镜像。保留在 tag 里。
