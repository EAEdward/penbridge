# 焦点笔输入归属图

适用设备：小米平板 8 Pro（`yingtian`），HyperOS 4.0.16.0.XBMCNXM / Android 17，KernelSU。

## 事件路径

```text
焦点笔 Pro（固件）
  -> HID over Bluetooth
  -> Xiaomi Focus Pen Pro Keyboard   /dev/input/event16  (input29)
       KEY_F19 (scanCode 189, keyCode 194)  = 轻捏
  -> InputReader / InputDispatcher
  -> 前台应用（StarNote 在 KEY_F19 的「抬起」上切一次工具）
```

本模块在 `event16` 上 `EVIOCGRAB`，于是系统看到的只有虚拟键盘：

```text
event16 ──EVIOCGRAB──> penbridge ──> uinput 虚拟键盘 /dev/input/event18 (input38)
                                        phys = penbridge-virtual
                                        bus 0005 / vid 0022 / pid 5081
```

## 归属与依赖

| 输入 | 判定者 | 仍依赖系统的部分 | 边界 |
| --- | --- | --- | --- |
| 轻捏 `KEY_F19` | `arbiter.c`：按下/抬起成对消费，每个边沿重放一对按键 | 应用自己的切换逻辑与其 300 ms 下限 | 无阈值、无最短时长 |
| 笔身双击、上下滑等其它键 | 不判定，原样转发 | 官方手势识别 | 只镜像、不改写 |
| 电磁/触摸事件（`event6/7/8`） | 不接管 | 系统手写笔服务 | 完全不碰坐标链路 |

## 设备上的其它 `KEY_F19` 节点

`sysfs capabilities/key` 扫描结果：

- `input29 / event16`：真实笔键盘接口，本模块独占的对象；
- `input38 / event18`：本模块创建的虚拟键盘（`phys=penbridge-virtual`，按 phys 排除，不会被误抓）；
- `input9 / event9`（`Xiaomi Keyboard`，bus 0006 / 15d9:00a3）与 `input12 / event12`
  （`Xiaomi Consumer`）也声明了 F19，但捏合时抓包为空，因此不接管。

## 下一轮设备核对

1. 记录安装二进制哈希与 ROM 指纹。
2. 分别验证「短促捏合（20–150 ms）」「常速捏合（300–600 ms）」「按住不放（> 2 s）」三种情形，
   确认工具在按下时变成橡皮、松手后回到画笔。
3. 连捏两次（间隔 200–500 ms），确认最终相位正确、没有多切一次。
4. 笔休眠后重新拿起，确认日志出现新的 `grabbed` 且功能恢复。
5. 关掉模块（`action.sh`）后确认恢复原厂行为。