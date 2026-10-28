# PenBridge — 小米焦点触控笔「捏住」→ 系统级 S Pen 按键

把小米焦点触控笔（Focus Pen / Focus Pen Pro）的**捏住 / 松开**变成真正的
`BTN_STYLUS`，让任何 app 都认为手里是一支**按住侧键的电磁笔**：
捏住 = 橡皮，松开 = 画笔。同时独占笔的按键接口，**彻底屏蔽小米官方手势**
（轻捏快捷键、双击、上下滑、截图、速记、激光、鼠标模式）。

设备实测：小米 Pad 8 Pro（`yingtian`，HyperOS 4 / Android 17 / KernelSU），
焦点触控笔 Pro（VID `0x0022` / PID `0x5081`）。

---

## 1. 为什么不是 LSPosed

一开始按 LSPosed 思路调研，确实有三个现成模块：

| 项目 | 作用 |
|---|---|
| [FocusPen Pro X](https://github.com/HMQYHM/FocusPenProX) | 小米焦点笔 Pro 手势增强（轻捏/连捏/按住/滑动），已适配 HyperOS 3–4 |
| [FocusPen](https://github.com/WondersReady/FocusPen) | 标准版焦点笔按键、激光、鼠标增强（开源，有设计文档） |
| [MaxMiPad](https://github.com/Xposed-Modules-Repo/com.yifeplayta.maxmipadinput) | 更早的小米平板输入增强 |

从它们的源码里挖到了关键事实：笔的捏合是蓝牙 HID 键盘设备
`Xiaomi Focus Pen Pro Keyboard` 上的 **KEY_F19**（scanCode 189），
而且**按下和抬起是分开上报的**（FocusPen Pro X 的“按住”手势就是拿 650ms
计时器在 DOWN/UP 之上自己算的）。也就是说笔本来就能区分「按住」和「松开」，
只是 HyperOS 只把抬手当成一次点击用。

但 LSPosed 只能改 Java 层，改不动输入事件本身：
MotionEvent 在 native 的 InputReader 里就已经带着 buttonState 了，
系统层 hook 拿到的只是“已派发”的事件。所以最后没走 Xposed，走的是
**evdev + uinput**，这也是为什么它对所有 app（包括游戏、原生渲染的笔记 app）都生效。

## 2. 原理

```
             ┌──────────────────── 小米平板 ────────────────────┐
 笔尖/压感 ──► Xiaomi_Touch_Stylus_Input_0_1 (event8) ─┐        │
 捏住      ──► Focus Pen Pro Keyboard     (event16) ──┐│        │
                                                      ││        │
                              ┌───────────────────────┘│        │
                              │  penbridge (root)      │        │
                              │  · EVIOCGRAB event16   │        │
                              │    → 官方手势全部失效   │        │
                              │  · EVIOCGRAB event8     │        │
                              │  · uinput 克隆一支同名  │        │
                              │    同 id 的笔 + BTN_STYLUS       │
                              └───────────┬────────────┘        │
                                          ▼                     │
                     Xiaomi_Touch_Stylus_Input_0_1 (event18)    │
                     事件流里多出 BTN_STYLUS 按下/抬起            │
                                          ▼                     │
                              所有 app：BUTTON_STYLUS_PRIMARY    │
```

要点：

* **独占（EVIOCGRAB）** 原始按键设备 → 小米系统再也收不到 F19，官方手势全灭。
* **克隆** 原始电磁笔设备（同名、同 VID/PID、同 ABS 量程 0–16383、同倾角、
  同 INPUT_PROP、同 phys），事件 1:1 转发，因此压力/倾斜/悬停/掌拒都不变。
* 克隆设备天然带 `BTN_STYLUS` 能力，捏住时在**同一个 SYN 帧里**注入
  `BTN_STYLUS 1`，松开注入 `0`。对上层来说和真实侧键没有区别。
* 笔离开感应范围（`BTN_TOOL_PEN 0`）自动释放按键，避免状态卡死。

## 3. 安装

```bash
# 方式 A：KernelSU / Magisk 管理器直接刷入
penbridge-v0.2.zip

# 方式 B：手动
adb push module /data/local/tmp/pbm
adb shell su -c 'mkdir -p /data/adb/modules/penbridge && cp -r /data/local/tmp/pbm/* /data/adb/modules/penbridge/ && chmod -R 755 /data/adb/modules/penbridge'
# 重启，或手动起一次：
adb shell su -c 'setsid sh /data/adb/modules/penbridge/service.sh'
```

日志：`/data/local/tmp/penbridge.log`

## 4. 配置

`/data/adb/modules/penbridge/config.txt`（改完重启，或手动重启服务）：

| 键 | 默认 | 说明 |
|---|---|---|
| `ROLE` | `solo` | 单机模式 |
| `KEYBOARD` | `Focus Pen` | 笔的按键设备名关键字（必须带 KEY_F19） |
| `STYLUS` | `Xiaomi_Touch_Stylus` | 要克隆的电磁笔设备名关键字 |
| `CLONE_NAME` | 空 | 可选，伪装克隆设备的显示名（某些 app 按名字认 S Pen 时填 `sec_e-pen`） |

## 5. 开关 / 排错

```bash
# 暂停（笔恢复成普通无按键笔）
adb shell su -c 'touch /data/adb/penbridge.disabled; pkill -f penbridge'

# 恢复
adb shell su -c 'rm /data/adb/penbridge.disabled; setsid sh /data/adb/modules/penbridge/service.sh'
```

KernelSU 管理器里的「操作」按钮 = `action.sh`，一键开关。

自检：

```bash
adb shell /data/adb/modules/penbridge/bin/penbridge list   # 看设备名/能力
adb shell getevent -pl /dev/input/event8                   # 原始笔能力
adb shell getevent -lt /dev/input/event18                  # 克隆流里应有 BTN_STYLUS
```

如果模块异常，最坏情况是 penbridge 没起来：此时没有 grab，笔照常写字，
只是没有橡皮键——不会把笔写坏。

## 6. 构建

```bash
# 任意 aarch64-linux 工具链均可；这里用 zig（单文件、无需 NDK）
zig cc -target aarch64-linux-musl -O2 -static -fno-PIE -no-pie -s \
    -o module/bin/penbridge module/src/penbridge.c
```

源码只有一个文件：`module/src/penbridge.c`（单机 `solo` 模式 +
跨设备 `tx`/`rx` 模式；跨设备模式是早期方案，笔尖不兼容时没用上，保留备用）。

## 7. 已知边界

* 只对**读 MotionEvent 的 app** 有意义；app 自己要支持侧键（StarNote、三星笔记、
  Squid、概念画板等都支持）。
* 若某个 app 靠“设备必须叫 sec_e-pen / 三星 VID”才认 S Pen，用
  `CLONE_NAME=sec_e-pen` 伪装。
* 捏住的判定完全依赖笔固件上报的 KEY_F19；笔没连上平板时没有按键可注入。
* 独占按键设备会同时干掉小米的“悬停预览/焦点激光/笔身滑动”，这是刻意的。
