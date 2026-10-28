# CHANGELOG

## v1.5 — 2026-10-28

* 合成点按从 20 ms 拉到 45 ms（`TAP_MS`）：更不容易被 framework / app 当成误触吞掉，
  仍然远低于长按判定（500 ms）和按键自动重复的延迟，不会触发别的语义。
* 想更松或更紧只改这一个常量，重新编译即可。

## v1.4 — 2026-10-28（实机重启验证通过）

* 结构性修正：`service.sh` 只启动**一次**（`setsid`，不再 supervise），
  重连循环搬进 daemon 自己（抢不到设备就 sleep 2 重试，永不主动退出）。
  旧版每 5 秒 fork 一次、日志刷 `already running` 的问题消失。
* 顺手修掉「重复 DOWN」的双击风险：按住期间再来的 DOWN 直接忽略，
  不再连发两次点按（那会被 framework 当成双击手势）。
* 开机日志改为 `--- <日期> boot launch`，一眼能分辨新旧版本。
* 已实机验证：重启后单进程、日志只有一条 boot launch + grabbed，
  虚拟键盘 `Location: penbridge-virtual` 与真实笔并存，注入的 keyCode 194
  能正常送达前台 app。

## v1.3 — 2026-10-28

* 跳过自己造的虚拟键盘（`phys=penbridge-virtual`），修掉「重启后抓到幽灵设备、注入全废」。
* 虚拟键盘镜像真实设备的全部 KEYbit，双击/上下滑继续可用。
* `flock` 单实例；framework 软重启后自动重建虚拟键盘。

## v1.2 — 2026-10-28

* 从「克隆整支笔」改为「独占键盘接口 + 重放轻捏」，只占一个设备。

## v0.2 — 2026-10-28（已实机验证）

* 单机模式 `solo`：在小米平板上完成 捏住 → BTN_STYLUS 的系统级映射。
* EVIOCGRAB 独占笔的按键接口（KEY_F19），小米官方手势全部失效。
* uinput 克隆电磁笔（同名、同 VID/PID、同 ABS 量程/倾角/属性），事件 1:1 转发。
* 打包为 KernelSU/Magisk 模块：开机自启、日志轮转、`action.sh` 一键开关。

## v0.1 — 2026-10-28

* 跨设备方案（tx/rx 模式）：UDP 转发捏合状态，三星端注入 BTN_STYLUS。
* 实测小米笔尖与三星 Wacom 电磁屏不兼容，方案作废，代码保留备用。
