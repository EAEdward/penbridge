# CHANGELOG

## v0.2 — 2026-10-28（已实机验证）

* 单机模式 `solo`：在小米平板上完成 捏住 → BTN_STYLUS 的系统级映射。
* EVIOCGRAB 独占笔的按键接口（KEY_F19），小米官方手势全部失效。
* uinput 克隆电磁笔（同名、同 VID/PID、同 ABS 量程/倾角/属性），事件 1:1 转发。
* 按键与坐标在同一个 SYN 帧内注入；`BTN_TOOL_PEN=0` 时自动释放，避免卡键。
* 支持多支笔节点（0_0 / 0_1）同时克隆。
* 打包为 KernelSU/Magisk 模块：开机自启、日志轮转、`action.sh` 一键开关。

验证环境：小米 Pad 8 Pro（yingtian，OS4.0.16.0.XBMCNXM / Android 17 / KernelSU 32680），
小米焦点触控笔 Pro（VID 0x0022 / PID 0x5081），StarNote 1.5.4 —— 捏住橡皮、松开画笔正常。

## v0.1 — 2026-10-28

* 跨设备方案（tx/rx 模式）：UDP 转发捏合状态，三星端注入 BTN_STYLUS。
* 实测小米笔尖与三星 Wacom 电磁屏不兼容，方案作废，代码保留备用。
