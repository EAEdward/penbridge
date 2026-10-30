# PenBridge

把小米焦点触控笔的「轻捏」从**点一下切一次工具**改成**按住切橡皮、松手切回画笔**。

系统原本：捏一下 = 切一次。改完：按下 = 切一次（→ 橡皮），一直按住就一直保持，
松手 = 再切一次（→ 画笔）。按住多久都不受影响，也没有「快速点按」这种特殊情形。

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

**本模块不定义「捏多用力算捏」**。捏合的力度与灵敏度在笔的固件和系统设置里
（`settings get system stylus_pinch_pressure_adjust`、`stylus_pinch_status`），
我们只做一件事：捏合按下 → 一次普通按键，松开 → 一次普通按键，其余交给系统。

唯一强制的是**两次按键不能挤在一起**：上一段的抬起还没结束就来的按下会被内核当成
重复状态丢掉，第二次就没了——「短促捏一下切过去回不来」正是这么来的。现在两次按键
至少隔 80 ms，正常捏合完全不受影响，只有极短的捏合会被压到 ~130 ms 内完成来回。

## 特性

- 单进程常驻，自己负责断连重扫；抢不到设备也不退出，2 秒后重试
- `flock` 单实例：软重启后残留的旧进程不会和新进程抢设备
- 只吞 `KEY_F19`，双击与滑动照常
- 笔休眠断连后自动重扫、重新独占
- framework 软重启（`system_server` 变化）或虚拟键盘消失时自动重建
- 39KB 静态二进制，空闲 0% CPU
- 关掉即完全恢复原厂

## 安装

```sh
# 作为 KernelSU / Magisk 模块（推荐）
adb push module /data/local/tmp/penbridge
adb shell su -c 'cp -r /data/local/tmp/penbridge /data/adb/modules/penbridge && chmod -R 755 /data/adb/modules/penbridge'
# 重启即自动启动；也可以手动起一个：
adb shell su -c 'setsid /data/adb/modules/penbridge/bin/penbridge </dev/null >>/data/local/tmp/penbridge.log 2>&1 &'
```

开关：`su -c sh /data/adb/modules/penbridge/action.sh`（写 `/data/adb/penbridge.disabled` 并杀掉进程，再按一次恢复）。

## 验证

```sh
adb shell su -c 'tail -f /data/local/tmp/penbridge.log'
# 捏一下应该出现：
#   [penbridge] down -> toggle
#   [penbridge] up -> toggle back
adb shell dumpsys input | grep -i "focus pen"   # 应有 2 个（真实 + 虚拟，虚拟的 Location 是 penbridge-virtual）
```

开机正常时日志开头是 `--- <日期> boot launch`，跟着一条 `grabbed /dev/input/eventNN`。
若看到每 5 秒一条 `already running`，说明系统里还留着旧版本的 supervisor，杀掉重来即可。

## 边界

只对「官方轻捏有动作」的场景生效（小米创作，或把轻捏设为切换画笔/橡皮的系统设置）。只认 `BTN_STYLUS` 的第三方笔记 app 不会因此获得橡皮。

## 构建

```sh
zig cc -target aarch64-linux-musl -O2 -static -fno-PIE -no-pie -s -o bin/penbridge src/replay.c
```

## 历史

`v0.2` 是早期方案：克隆整支电磁笔并注入 `BTN_STYLUS`，可让任何认按键的 app 获得橡皮，但要多一个设备、多一份事件镜像。保留在 tag 里。
