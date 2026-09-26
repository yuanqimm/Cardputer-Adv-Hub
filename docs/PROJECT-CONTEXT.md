# Cardputer Adv Hub 开发接续文档

更新日期：2026-09-26。新任务先阅读本文件，再看 README 操作说明和 features-validation.md 验证记录。

## 用户目标和当前阶段

- 设备：M5Stack Cardputer-Adv；以开发板和 SD 卡为主要硬件。
- 功能目标：便携 SSH、USB/BLE 双模键盘、红外遥控、SD 音乐/视频、简洁独立页面；保留后续扩展能力。
- 优先整合已有方案，保留第三方许可证；缺少的部分自行实现。
- 当前仍为 PlatformIO + Arduino + VS Code。后续路线是 ESP-IDF + Arduino Component，不是已经完成迁移。
- USB SD 手动共享已获用户确认。最新需求：Media Player 更名 SD Storage，改为类似电脑的文件夹浏览及 SD 读写操作，同时支持音频/视频，并参考已有项目。

## 工程和工具

- 本机目录：`C:\Users\yuanman\Desktop\IOT\M5stack\Cardputer Adv Hub`。
- 仓库：<https://github.com/yuanqimm/Cardputer-Adv-Hub>，分支 `main`。
- Git 身份：yuan / 3127008671@qq.com。用户已授权建立 Git 管理和推送该仓库。
- PlatformIO：`C:\Users\yuanman\.platformio\penv\Scripts\platformio.exe`。
- 环境：`cardputer-adv`，espressif32 7.1.3，Arduino-ESP32 2.0.17 系列打包版本，ESP32-S3 8 MB Flash、无 PSRAM。
- 依赖精确版本见 platformio.ini；不要为排障随意升级 BLE/USB 相关依赖。
- 最近下载端口 COM12，运行端口 COM17；必须重新检测，不能写死。早期 COM8 和带冒号的 ROM 序列号是历史记录。

## 功能状态与代码入口

| 功能 | 入口 | 状态 |
|---|---|---|
| 页面、快捷键 | src/apps/LauncherApp.cpp | 主页五项；USB SD 位于 Settings 第四项，独立页面 screen 6 |
| 键盘 | KeyboardManager / UsbKeyboardService / BleKeyboardService | 用户曾确认 USB、Win11 BLE 和小米 14 N2 成功；完整键位回归清单保留 |
| Wi-Fi / SSH | src/core/SshService.cpp | 扫描、密码输入、NVS 记忆、SSH 登录、主机指纹和交互终端已实现；完整真实服务器验收见验证文档 |
| 红外 | src/core/IrRemote.cpp | GPIO44 发射，多设备 JSON；真实协议/地址/命令需设备验收；学习需外接接收器 |
| SD Storage | src/apps/SdStorage.cpp、src/core/FileManager.cpp | 多级目录、文件读写/复制/移动/删除/属性、4 KiB 文本编辑；64 项分页；新功能待实机验收 |
| 音视频 | src/media | 从任意文件夹打开 MP3/WAV、JPEG/原始 MJPEG；不直接支持 MP4/H.264，播放器完整实测尚待记录 |
| USB SD | src/core/UsbStorageService.cpp | 2026-09-26 用户确认手动共享功能完成 |
| 显示 | src/core/Ui.cpp | 240×135、8 位帧缓冲和内容哈希抑制重复刷屏；用户确认闪烁已解决 |

## USB SD 已接受的行为

1. 每次开机默认 OFF，不把开关持久化。USB 接口始终注册，关闭时报告无介质，因此 Windows 可能显示未就绪的空读卡器。
2. Settings → USB SD sharing → Enter 开启，才允许电脑读写。无有效 SD 卡时拒绝开启。
3. 开启前停止音视频、关闭文件、暂停设备端扫描及 SSH/红外 SD 配置读取，然后才设置 mediaPresent(true)。
4. USB 回调只进行受互斥锁保护的扇区 I/O；支持跨扇区和部分扇区访问，禁用及越界请求返回错误。
5. Fn+Q 返回主页保留共享状态。hostActive() 表示共享已启用，不代表主机正在传输。
6. SCSI 安全弹出或 USB STOPPED 事件请求退出，由 Arduino loop 关闭介质、等待正在进行的 I/O、重新挂载文件系统以丢弃 FAT 缓存，再刷新目录。
7. 手动退出：Enter 提示先在电脑弹出，Y 关闭，N 取消。不能在电脑写入期间强行切换。

### 必须保留的实现约束

- ARDUINO_USB_MODE=0、ARDUINO_USB_CDC_ON_BOOT=1。Arduino 在 setup 前启动 USB。
- HID、键盘、MSC 对象在 UsbKeyboardService.cpp 同一编译单元静态注册。UsbStorageService.cpp 使用该 MSC 对象；不能移到 setup 才创建，否则 MSC 不进入已生成的 USB 描述符。
- MSC 构造时默认无介质，setup 配置回调但不开放 SD；只由页面操作开放。
- SD 所有权切换、媒体关闭、目录扫描和重新挂载在 Arduino loop 执行。添加新 SD 使用者时必须检查 Storage::appAccessAllowed()；若改为后台任务，需要重新设计应用层同步。
- 共享结束用 Storage::remount()，不能只恢复布尔开关后继续使用旧 FAT 缓存。

## 不能回退的已有约定

- BLE 设备名 `Cardputer Hub N2`，NVS 固定静态随机地址、配对参数不随意改动或清空。Fn+R 是用户主动清配对操作。
- 仅 Keyboard 页面向 USB/BLE 主机发送输入；进入等待松键、退出释放，Opt 对应 Win/Command。
- 离开 SSH 断开 SSH 会话但保留 Wi-Fi；主页状态栏继续显示 Wi-Fi 状态。
- SSH 内存有限：主循环栈 51200 字节，握手前暂时释放 BLE，结束后恢复。不要仅降低内存阈值掩盖分配失败。
- 保持帧缓冲防闪烁策略，不因普通按键无条件刷新屏幕。

## 构建和验证

```powershell
platformio run
platformio device list
platformio run --target upload --upload-port COM12
g++ -std=c++11 -Wall -Wextra -Werror -I include tests/native/test_core.cpp -o test-core.exe
./test-core.exe
g++ -std=c++17 -Wall -Wextra -Werror -I tests/native/storage_stubs -I include src/core/FileManager.cpp tests/native/test_file_manager.cpp -o .diagnostics/test-file-manager.exe
./.diagnostics/test-file-manager.exe
git diff --check
```

上传命令里的端口需要替换成实测值。PlatformIO 需要写用户目录中的包锁和缓存；遇到沙箱 PermissionError 应按权限流程执行，而不是删除工具链或锁文件。

交付 USB SD 固件已编译并烧录校验成功，正常重启后检测到 MSC/HID/CDC 以及 COM17。共享关闭时 E: 未就绪；随后用户确认功能完成。完整压力测试、文件哈希校验和其他功能的回归未逐项记录，不能声称全部通过。

其后的 SD Storage 固件已构建并通过 COM12 烧录/写入校验，RAM 88332、Flash 1583833 字节，两套原生测试通过。烧录后仍枚举 ROM 下载端口，已请用户不按 G0 正常重插，等待确认启动和文件列表；不能把此状态解释为应用崩溃，新增操作的实机验收仍待完成。

此前的 `boot:0x3 (DOWNLOAD)` / `waiting for download` 只说明处于下载模式；不能据此认定应用崩溃、硬件 USB 抢占或 G0 一直被按住。没有捕获确定重启根因的 panic 栈，后续如复发应先取日志。

## 后续开发和资料

- 已完成 USB SD 的常规交付；仅在出现新证据或回归失败时继续排障。
- 本轮 SD Storage：已参考 Bruce 文件浏览器/音频页面与 M5Stack 官方 SD 示例，未复制 Bruce AGPL 源码；引用及设计边界见 docs/sd-storage-reference.md。
- 已移除旧 `/music`、`/video` 常驻索引及上一首/下一首接口；文件管理是唯一播放入口，播放结束停止。媒体分配前释放目录列表。
- FileManager 检查 SD 所有权；Storage::suspendAppAccess 取消复制并释放列表，归还时重新挂载。文本编辑未保存退出需确认，文件操作不覆盖同名目标。
- 后续先按 features-validation.md 验收新 SD Storage；真实 SSH、媒体和红外的完整测试状态仍应区分记录。
- README：当前使用说明、文件格式和开发命令。
- docs/features-validation.md：用户反馈、已执行证据与待测边界。
- docs/BLE-N1-validation.md：N1 → N2 历史，不能把其中 N1 故障当成当前状态。
- `.diagnostics/`：本机调试资料、基线备份，不提交。构建产物位于 `.pio/`；临时串口脚本已从项目根目录移除。
- sd-card/config 是公开占位模板；真实凭据放到被忽略的 config/ 或实际 SD 卡，不提交。
