# BLE N1 验证记录（2026-09-20）

## 已知现象

- 用户确认旧版屏幕不再闪烁、USB 键盘可输入、Win11 BLE 可连接。
- 小米 14 使用旧 BLE 实现仍提示无法通信，曾在 paired / encrypted 与 advertising 间切换。
- 旧配置 `ARDUINO_USB_MODE=1` 启动 TinyUSB HID 后，Windows 只枚举 HID，无法保留诊断串口。没有抓到本次手机断开的有效 BLE 日志，因此尚不能确定旧版断开根因。

## 本次修改

- 原生 NimBLE-Arduino 1.4.3 替代旧 BLE 键盘封装，广播名 `Cardputer Hub N1`。
- 标准键盘输入/LED 输出、Boot 特征、按主机订阅和加密状态发送报告。
- 保留断开原因、加密错误码、连接/断开次数和重启原因；串口连接后输出状态快照。
- Keyboard 页面 Fn+R 主动清除 NimBLE 配对记录。
- USB 模式改为 TinyUSB HID + CDC，并移除开发板默认 MODE=1 编译宏。
- 保留屏幕缓冲绘制逻辑。

## 验证结果

- PlatformIO 编译成功：RAM 静态占用 83812 字节，Flash 1515669 字节。此数值不是运行时剩余堆内存。
- HID 描述符解析检查通过：Report ID 1，输入 64 位、输出 8 位，Collection 配对闭合。
- COM12 烧录成功，写入校验通过，见 `upload-nimble.log`。
- 初次烧录后仍枚举为 ROM USB Serial/JTAG，用户正常重启后确认 N1 界面已启动。
- Windows PnP 确认同时存在 HID Keyboard Device 和 COM8 CDC 串口，USB 复合设备枚举通过。
- COM8 暂未捕获到文本日志；不能据此判断蓝牙是否连接成功。
- 用户复测 N1：小米 14 仍提示无法通信，未显示 HID ready。此次迁移没有解决该手机的连接问题。
- 已请求失败后屏幕 N1 / Last / Auth / Sub / C / D / Reset 原始数值；保持开机保留证据。
- Win11 和 USB 输入回归：待用户实测。

## 后续证据与 N2 隔离测试

- 用户提供 N1 开机后状态：advertising，Last/Auth/Sub 都为 0，C/D 都为 0，Reset=1。
- 电脑实测收到 N1 实时广播，地址 AC:A7:04:01:38:FD，RSSI -36 dBm。广播类型 ConnectableUndirected，地址类型 Public，Flags=06，Appearance=03c1，HID Service=1812。
- Windows Bleak 读取服务返回 Unreachable。诊断时暂时打开电脑蓝牙，finally 恢复原来的关闭状态。
- 此后用户读到 C=66、D=66、Last=0x208。0x208 为 NimBLE HCI connection timeout（0x200 + 0x08）；成功建链计数已发生变化，不能用先前全零推断整个测试没有建立链路。
- N2 用保存在 NVS 的静态随机地址隔离旧公共地址缓存；只改名字不能隔离蓝牙身份。缓存问题是测试假设，尚未证实。
- N2 增加 F（链路建立失败次数），Last 同时保留最近的非零建链失败码，成功连接后恢复显示断开码；串口发送问号可请求快照，包含实际广播状态、地址和连接数。
- N2 编译及 COM12 烧录校验成功（静态 RAM 83900 字节、Flash 1519581 字节）。普通 COM8 烧录握手失败后，用 CDC 1200 波特率成功自动进入下载模式，无需手动按键。
- 重启后 COM8 可读取问号快照；已观察新地址 E9:A8:B4:3E:2A:0F，随机地址类型 1，可连接广播。
- Windows 无缓存 GATT 连接成功，读到 GAP、GATT、Device Information、Battery 服务；测试主动断开日志为 0x215，电脑蓝牙已恢复关闭。这不等于 Windows HID 输入回归通过。
- 后续串口快照曾达到 HID ready、Sub=3、C=2、D=1、F=0；随后主机取消订阅并以 0x213（远端主动结束）断开，encrypted=1。需结合用户反馈判断手机输入结果，不能仅凭此快照宣布修复。

## 用户最终确认

- 用户对 N2 手机复测回复：“太好了，成功了”。小米 14 连接问题在本次实测中恢复。
- 同期日志显示 encrypted=1、bonded=1、report subscription=1、boot subscription=1，重新连接也再次完成加密和订阅。证据见 `ble-n2-live.log`。
- 保留 N2 固件与固定 NVS 地址，不再改动配对参数。该结果支持旧设备身份缓存参与故障的判断，但未取得手机底层日志，不将其表述为唯一已证实根因。
- 未在本次逐项验证全部按键、长按、断电回连以及 Win11/USB 全量回归；此前的屏幕缓冲修复保持不变。

不能以编译或烧录成功代替 Android 兼容性结果。
