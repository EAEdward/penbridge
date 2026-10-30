# PenBridge · 焦点笔轻捏映射

把小米焦点触控笔 Pro 的「轻捏」从**捏一次切一次工具**改成**捏住就是橡皮，松手回到画笔**。
只改输入映射，不动任何 app。

原厂行为：捏一下 = 一次 `KEY_F19` 按下 + 抬起，前台应用在**抬起**时切一次工具，于是工具停在橡皮，
要再捏一次才切回来。本模块把这一次手势拆成两次：按下那一下就是一次完整的按键，抬起那一下也是。

## 功能

- **捏下 → 橡皮，松手 → 画笔**。按住多久都保持橡皮，松手立刻切回，没有「快速点按」这种特殊分支。
- 只接管轻捏（`KEY_F19` / keyCode 194），笔身双击与上下滑原样转发，官方手势不受影响。
- 单进程常驻，空闲 0% CPU；笔休眠断连后自动重新独占。
- 设备被别的进程抢走时不会硬跑：抢不到就 2 秒后重试，绝不出现「一次捏合切两次」。
- framework 软重启（`system_server` 变化）或虚拟键盘消失时自动重建。
- 关掉即完全恢复原厂行为。

## 时间约束

每次切换都是一次完整的按键（按下 + 抬起在同一批里写出，中间没有任何等待），
所以**按下和松手本身都是瞬发的**。唯一的限制来自前台应用自己：

StarNote 的工具栏在同一次切换后约 300 ms 内会丢弃下一次切换。实测（同一台机器、同一条注入链路，
每组 3–6 次）：

| 两次切换间隔 | 第二次是否生效 |
| --- | --- |
| 120 ms | 全部丢弃 |
| 180 ms | 全部丢弃 |
| 200 ms | 4/6 |
| 250 ms | 4/4 |
| 300 ms | 6/6 |

因此模块对「两次切换」只做一件事：不允许比 300 ms 更密。**捏合时间超过 300 ms 时不会增加任何延迟**；
短促捏合（20–150 ms）时，橡皮会在手上停留到 300 ms 再切回——这是应用的下限，不是模块的阈值。

## 原理

```text
Xiaomi Focus Pen Pro Keyboard (evdev, event16)
      │  EVIOCGRAB 独占：系统再也收不到原始轻捏
      ▼
  penbridge ── 归属仲裁：一次捏合 = 一对按下/抬起，成对消费
      │        · 按住期间的重复按下不算新边沿
      │        · 没有按下配对的抬起是孤儿，丢弃（否则工具会自己翻）
      ▼
虚拟键盘 (uinput, phys=penbridge-virtual, bus 0005 / vid 0022 / pid 5081)
      │  每个边沿写出一对 KEY_F19 DOWN+UP（同一批，零间隔）
      ▼
系统照常执行官方轻捏 → 前台应用切一次工具
```

「捏多用力算捏」不在本模块里：力度与灵敏度在笔固件和系统设置
（`settings get system stylus_pinch_pressure_adjust`、`stylus_pinch_status`）。

## 安装

```sh
sh install.sh [serial]          # KernelSU / Magisk 模块
```

手动安装与开关：

```sh
adb push module /data/local/tmp/penbridge
adb shell su -c 'cp -r /data/local/tmp/penbridge /data/adb/modules/penbridge && chmod -R 755 /data/adb/modules/penbridge'
adb shell su -c 'setsid /data/adb/modules/penbridge/bin/penbridge </dev/null >>/data/local/tmp/penbridge.log 2>&1 &'
adb shell su -c 'sh /data/adb/modules/penbridge/action.sh'   # 开关
```

## 验证

```sh
adb shell su -c 'tail -f /data/local/tmp/penbridge.log'
# [penbridge] pen keyboard: Xiaomi Focus Pen Pro Keyboard (/dev/input/event16)
# [penbridge] virtual keyboard ready: "Xiaomi Focus Pen Pro Keyboard" bus=0005 vid=0022 pid=5081
# [penbridge] grabbed /dev/input/event16
adb shell dumpsys input | grep -i 'focus pen'   # 应有 2 个，虚拟的那个 Location 是 penbridge-virtual
```

开机日志开头是 `--- <日期> boot launch`。若反复出现 `already running`，说明系统里还有旧进程，杀掉重来。

验收清单见 [docs/DEVICE_TEST_CHECKLIST.md](docs/DEVICE_TEST_CHECKLIST.md)。

## 边界

- 只对「按键抬起时切换工具」的应用有效（StarNote，以及把轻捏设为切画笔/橡皮的系统场景）。
  只认 `BTN_STYLUS` 的第三方笔记 app 不会因此获得橡皮。
- 锁屏、息屏、游戏等由系统另行处理的场景不在本模块处理范围。
- 虚拟键盘保留真实笔的全部 KEYbit，只有轻捏被重写；双击/滑动走原样转发这条路径。

## 构建

```sh
zig cc -target aarch64-linux-musl -O2 -static -fno-PIE -no-pie -s -o bin/penbridge src/*.c
```

代码分为 `pen_device.c`（找设备并独占）、`arbiter.c`（归属与边沿策略，全部规则都在这里）、
`replay.c`（虚拟键盘与成对重放）、`main.c`（生命周期与看门狗）。设计与边界见
[docs/DESIGN.md](docs/DESIGN.md)，事件路径见 [docs/INPUT_ROUTING.md](docs/INPUT_ROUTING.md)。

## 许可

Apache-2.0。输入归属仲裁与「一次手势一对按键」的做法参考了
[WondersReady/FocusPen](https://github.com/WondersReady/FocusPen) 的公开设计说明。