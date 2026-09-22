# Cardputer Adv Hub

M5Stack Cardputer-Adv 的 SSH 终端、USB/BLE 双模键盘、红外遥控和 SD 媒体中心。开发环境为 VS Code + PlatformIO + Arduino，保留后续 ESP-IDF + Arduino Component 的迁移路线。当前是逐步完善的固件，不把编译通过视作所有硬件功能已经实测。

## 编译和烧录

```powershell
platformio run
platformio run --target upload --upload-port COM12
platformio device monitor --port COM8 --baud 115200
```

端口以电脑实际枚举为准。正常运行使用 TinyUSB HID + CDC；下载模式通常为 COM12，运行时通常为 COM8。若自动下载握手失败，可通过 CDC 1200 波特率进入下载模式，或按住 G0/BOOT 重新插入 USB 后松开。依赖版本固定在 `platformio.ini`。

## 日常操作

| 页面 | 操作 |
|---|---|
| 启动器 | W/S、K/J 或 Fn 方向键选择，Enter 进入 |
| 全部页面 | **Fn+Q 返回首页**；离开 SSH 会断开会话，离开媒体会停止播放 |
| Keyboard | Fn+M 切换 USB / Bluetooth / 双发，Fn+D 查看诊断，Fn+R 清除全部 BLE 配对 |
| SSH | C 扫描附近 Wi-Fi，W/S 或 Fn 方向键选择，Enter 连接；H 打开已成功连接的历史配置；D 直接使用 SD 配置；首次遇到主机时校验指纹后按 T 信任并保存 |
| 音乐 | P 播放/暂停/继续，N/B 下一首/上一首，+/- 音量，V 切换视频 |
| 视频 | P 播放/暂停/继续，N/B 换文件，V 切换音乐；到文件尾停止 |
| 媒体 | R 重新扫描 SD 文件 |
| 红外 | 1–9 发射对应按键，N 切换设备，R 重载配置 |
| 设置 | W/S 选择，+/- 或 Fn 左右方向键调整；亮度、音量、视频帧率自动保存 |

键盘映射：Fn+`;` 上、Fn+`.` 下、Fn+`,` 左、Fn+`/` 右、Fn+反引号 Escape、Fn+Backspace Delete。Opt 映射 Win/Command。USB 支持保持按键直至实际松开，主机负责长按重复；SSH 与菜单支持本地重复。

**只有 Keyboard 页面会向外部 USB/BLE 主机发送按键。** 进入页面时会等待进入键松开，退出时发送释放报告。普通 `q`、`c`、`x`、Backspace 在 Keyboard 和已连接的 SSH 终端中是输入内容，不再被页面快捷键抢占。其余非输入页面仍兼容 Q/Backspace 返回。

## SD 卡

使用 FAT32。将 `sd-card` 下的目录复制到卡根目录，并修改 Wi-Fi/SSH 配置中的占位内容：

```text
/music/hub-demo.wav    两秒、较低幅度的测试音频
/video/               JPEG 图片或原始 MJPEG 文件
/config/wifi.json
/config/ssh.json
/config/ir.json
```

启动时会创建缺失的目录，但不会自动写入账号或遥控配置。每个媒体目录最多索引 128 个文件，按文件名排序，忽略隐藏文件；文件名最多 140 字节。不支持递归子目录。音频列表只收 MP3/WAV，视频列表只收 JPG/JPEG/MJPG/MJPEG。

音乐显示文件名、播放状态、已输出采样的时长、音量和**文件读取进度**；读取百分比不等于精确的音频时间百分比。P 暂停后可继续同一曲目，多曲目正常到尾时自动播放下一首。切换音乐/视频时会关闭前一种播放器以释放内存。

视频支持 JPEG 图片轮播和无音轨的原始 MJPEG。MP4/H.264、普通 AVI、GIF **不能直接播放**。在电脑安装 ffmpeg 后转换：

```powershell
python tools/convert_video.py input.mp4 output.mjpeg --fps 12
```

转换为 240×96、12 fps，复制到 `/video`；设置页选择相同帧率。实际帧率受 SD 卡和解码耗时影响。单帧压缩数据上限 40 KiB，超限显示错误，不无限分配内存。转换脚本拒绝覆盖已有输出文件。普通 JPEG 使用原生 JPEG 解码器显示，过大图片会受显示区域裁剪。

## SSH

`/config/wifi.json`：

```json
{"ssid":"YOUR_WIFI_SSID","password":"YOUR_WIFI_PASSWORD"}
```

`/config/ssh.json`：

```json
{"host":"192.168.1.10","port":22,"user":"YOUR_SSH_USER","password":"YOUR_SSH_PASSWORD"}
```

进入 SSH 页面后按 C 会异步扫描附近 Wi-Fi，列表显示 SSID、信号强度和加密标记；扫描不会阻塞界面。选择网络后按 Enter 连接。开放网络可以直接使用，已记忆网络会自动取用已保存的 Wi-Fi 密码；未知的加密网络仍需先把 SSID 和密码写入 `/config/wifi.json`。

连接分阶段执行：Wi-Fi、握手、主机指纹、认证、PTY、shell。网络等待有超时，可用 Fn+Q 取消。首次连接显示服务器 SHA-256 公钥指纹（64 个十六进制字符），请与服务器端实际公钥核对后按 T；信任记录保存至 NVS。SSH 认证成功后，Wi-Fi SSID/密码、主机、端口、用户名、SSH 密码和指纹会保存为最多 6 条历史配置。按 H 打开历史列表，选择后按 Enter 可一键重连；已保存的主机指纹不匹配时停止认证，不发送密码。

也可在 `ssh.json` 中配置 `host_key_sha256`，值为事先核验的 64 个十六进制字符指纹；该显式指纹优先于已保存的记录。服务器更换主机密钥后需核验并更新该字段。配置文件应由用户自行保管。

历史配置保存在开发板 NVS，不写回 SD 卡；最多保存 6 条，新增配置达到上限时淘汰最早一条。密码仍应按个人设备安全要求管理。当前支持密码认证，尚未加入私钥认证、设备内密码编辑器与多主机会话管理。终端为 38 列 × 12 行，支持分段 ANSI 转义、光标定位、清屏/清行、滚屏、CR/LF、退格、Tab、Ctrl+C/D 等、Alt 前缀和方向键；UTF-8 非 ASCII 字符显示替代符。

LibSSH-ESP32 上游示例使用 51200 字节任务栈，本项目相应增加 Arduino 主循环栈；没有 PSRAM，启动 SSH 或媒体前会检查可用堆内存。

## 蓝牙 N2

设备名保持 **Cardputer Hub N2**。2026-09-20 用户确认小米 14 连接成功，日志确认加密、保存配对和 HID 订阅成功。N2 使用保存在 NVS 中的固定静态随机地址，普通重启不改变地址；本轮保留这一身份与配对配置。

实现基于 NimBLE-Arduino 1.4.3，提供标准键盘报告、LED 输出、Boot/Report 协议和主机订阅检测。Bonding + Secure Connections、NoInputNoOutput Just Works，不提供 MITM 防护。旧的 `lib/ESP32BLEKeyboard` 仅作参考并已排除编译。

BLE 同时连接一个主机；键盘页可独立选择 USB、BLE 或双发模式。多个曾配对主机都打开蓝牙时可能抢先回连，测试时只保留目标主机。Fn+R 会清除**开发板保存的所有 BLE 配对**，之后也应在相应主机删除旧记录重新配对；它不改变 N2 固定地址。

Fn+D 诊断页：Last 是最近的断开码或建链失败码，Auth 是加密失败码，Sub 是 Report=1/Boot=2 订阅位，C/D 是连接/断开计数，F 是建链失败次数，Reset 是 ESP32 重启原因。`0x208` 是链路超时。打开串口后发送 `?` 可读取状态、地址、广播/连接数与可用内存，不记录键盘输入内容。排障历史见 `docs/BLE-N1-validation.md`。

## 红外遥控

内置 GPIO44 红外发射，支持 NEC、Samsung、Sony、RC5。必须使用与目标设备匹配的协议、地址和命令；示例中的 Demo NEC 并非通用遥控码。没有内置接收器，学习原遥控器需要外接接收模块。

`/config/ir.json` 可以是一个设备对象，或最多 8 个设备的数组；每个设备最多 9 个按键：

```json
[
  {
    "name":"My TV",
    "protocol":"NEC",
    "address":"0x00",
    "repeats":0,
    "buttons":[
      {"label":"Power","command":"0x45"},
      {"label":"Volume -","command":"0x46"},
      {"label":"Volume +","command":"0x47"}
    ]
  }
]
```

数字支持十进制整数或 `0x` 字符串。NEC 命令为 8 位，Samsung 为 16 位；Sony/RC5 命令为 7 位，RC5 地址为 5 位。Sony 的 `bits` 可取 12、15、20，对应地址上限 31、255、8191。`repeats` 为 0–3。配置文件最大 16 KiB，无效配置保留原有效配置。兼容旧 `buttons.power / vol_minus / vol_plus` 格式。

## 验证与目录

`tests/native/test_core.cpp` 检查 ANSI 分段解析、光标和滚屏边界、MJPEG 帧上限/恢复、键位映射：

```powershell
g++ -std=c++11 -Wall -Wextra -Werror -I include tests/native/test_core.cpp -o test-core.exe
./test-core.exe
```

显示仍使用 240×135、8 位画面缓冲，完整绘制后提交，内容未变化时不刷新。本轮修改前已保存 N2 源码与固件到 `.diagnostics/baseline-n2`，该备份不含蓝牙 NVS 数据。

`src/core` 是硬件与网络服务，`src/media` 是解码/播放，`src/apps` 是页面和输入路由，`sd-card` 是示例资源，`tools` 是电脑辅助脚本。复用的本地 ESP8266Audio 保留原许可证。

本轮编译、烧录和原生测试的结果及待实测项目见 `docs/features-validation.md`。

## Git 版本管理

项目使用本地 Git 仓库，主分支为 `main`。源码、项目配置、文档、测试、SD 示例资源及本地依赖库纳入版本管理；构建缓存、运行日志、诊断工具和本机固件备份由 `.gitignore` 排除。

```powershell
git status
git log --oneline -5
git diff
```

后续按独立功能提交，并在提交说明中记录验证结果。`sd-card/config` 是受版本管理的占位示例，不应填写真实密码后提交；个人配置可先放在被忽略的根目录 `config/`，再复制到 SD 卡。
