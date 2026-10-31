# 设备验收清单

设备：小米平板 8 Pro（`yingtian`），HyperOS 4.0.16.0.XBMCNXM / Android 17，KernelSU。
笔：Xiaomi Focus Pen Pro。前台应用：StarNote（`com.onyx.galaxy.global.note`）。

## 0. 安装

- [ ] 记录 `md5sum /data/adb/modules/penbridge/bin/penbridge`（与本地构建一致）
- [ ] 记录 ROM 指纹：`getprop ro.build.fingerprint`
- [ ] 日志首行是 `--- <日期> boot launch`，随后有 `pen keyboard` / `virtual keyboard ready` / `grabbed`
- [ ] `dumpsys input` 里出现两个 `Xiaomi Focus Pen Pro Keyboard`，虚拟的那个 `Location` 是 `penbridge-virtual`
- [ ] 只有一个 `penbridge` 进程

## 1. 三种捏合

- [ ] 短促捏合（20–150 ms）：工具变橡皮，约 300 ms 后自动回到画笔
- [ ] 常速捏合（300–600 ms）：按下即橡皮，松手即画笔
- [ ] 按住不放（> 2 s）：整段保持橡皮，松手立刻画笔

## 2. 连击与误触

- [ ] 连捏两次（间隔 200–500 ms）：最终停在画笔，没有多切一次
- [ ] 刷笔身、手指蹭过：工具不变
- [ ] 捏到一半松手再捏：相位正确（橡皮 → 画笔）

## 3. 生命周期

- [ ] **重启设备**（笔保持休眠）：日志顺序应为 `boot launch` → `waiting for the pen` →（拿起笔后）`grabbed /dev/input/eventNN`
- [ ] 重启后**不允许**出现 `grabbed /dev/input/event9`（平板自带的 `Xiaomi Keyboard` 也声明 F19，抓错它就是 v2.0 的重启失效）
- [ ] 重启后第一次捏合就能用（不需要手动重启 daemon）

- [ ] 笔休眠后再拿起：日志出现新的 `grabbed`，功能恢复
- [ ] 软重启（`stop && start system_server` 或系统「重启界面」）：日志出现 `framework restarted, rebuilding virtual keyboard`，之后功能正常
- [ ] 连续开关模块（`action.sh`）：恢复原厂行为 / 重新接管

## 4. 不属于本模块

- 笔身双击、上下滑：仍由官方政策处理（本模块只转发，不判定）
- 电磁笔坐标（`event6/7/8`）：模块不参与