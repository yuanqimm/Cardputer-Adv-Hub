# SD Storage：参考与实现

2026-09-26，按用户要求将 Media Player 改为 SD Storage，以文件管理为入口保留音视频播放。

## 已阅读的上游资料

| 来源 | 固定版本与文件 | 参考范围 |
|---|---|---|
| [Bruce](https://github.com/pr3y/Bruce) | [sd_functions.cpp](https://github.com/pr3y/Bruce/blob/a59213f3ec6cd302fa32c2042b2700c9c24d991a/src/core/sd_functions.cpp) | 文件夹优先的列表、文件操作菜单、复制/粘贴、播放前释放列表内存 |
| Bruce | [audio_player.cpp](https://github.com/pr3y/Bruce/blob/a59213f3ec6cd302fa32c2042b2700c9c24d991a/src/modules/others/audio_player.cpp) | 文件名、状态、进度及播放控制的页面组织 |
| [M5Stack M5Cardputer](https://github.com/m5stack/M5Cardputer) | [sdcard.ino](https://github.com/m5stack/M5Cardputer/blob/f1392858b9994c3547120e602a57d3553d16ab01/examples/Basic/sdcard/sdcard.ino) | 官方 Arduino SD/SPI 初始化及目录、文件读写、重命名和删除 API |

本轮查阅了 Bruce 的源码和 LICENSE（AGPL-3.0）。本项目只借鉴上述交互与资源管理思路，没有复制或移植其源码；文件操作服务及页面按现有 Hub 架构自行实现。M5Stack 示例用于核对 API 和现有 SD 引脚（SCK40/MISO39/MOSI14/CS12，25MHz），没有引入新的外部源码文件。继续使用项目已有 ESP8266Audio、M5GFX 和 M5Cardputer 依赖，保留原许可证。参考不是全量功能或兼容性承诺。

## 实现位置

- `src/apps/SdStorage.cpp`：文件夹、操作菜单、输入、确认、文本编辑、复制进度与播放页面。
- `src/core/FileManager.cpp`：目录分页、创建、改名、移动、删除、文本读取/保存、分段复制。
- `include/core/FilePath.h`：路径及文件类型规则；`TextCursor.h`：UTF-8/CRLF 光标边界和显示行移动。
- `src/media`：复用 MP3/WAV 解码和 JPEG/原始 MJPEG 播放，新增按完整路径打开的入口，移除旧固定目录索引。
- `Storage` / `UsbStorageService`：继续采用设备与电脑互斥的 SD 所有权；共享前关闭文件，归还后重新挂载。

## 数据与内存约束

每批目录保留最多 64 项，通过文件名和类型标记定位后续批次；翻批会重新扫描目录，因此大目录扫描耗时随条目数增长。文件复制每轮最多 4 KiB，允许界面处理取消。文本最多 4 KiB；先写 `.hub-tmp`，原件改名为 `.hub-bak`，替换成功后清理备份，失败时尝试恢复原件。意外断电时仍可能需要在电脑恢复文件，不能把这种策略描述成完整的文件系统事务。

所有写入避免覆盖同名目标。文件夹仅支持移动、改名和删除空目录；文件夹递归复制和非空目录递归删除未实现。原始 MJPEG 不带音轨、单帧不超过 40 KiB，MP4/H.264 仍需电脑转码。文本非 ASCII 显示替代符；文件名的非 ASCII 显示受当前字体限制，输入名称为 ASCII。

## 验证边界

主机测试直接编译生产文件服务，使用唯一临时目录模拟 SD，并注入写入/重命名失败；测试并不模拟真实 FAT 驱动、SD 断电或 USB 时序。固件构建和主机测试通过后，仍需开发板确认键盘操作、真实 SD 读写、媒体解码以及共享前后的目录刷新。详细结果见 `features-validation.md`。
