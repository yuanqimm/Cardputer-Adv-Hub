# 红外功能参考与实现边界

本轮实现参考了以下公开项目和示例：

- [Arduino-IRremote](https://github.com/Arduino-IRremote/Arduino-IRremote) 的 `SendDemo`、`SendRawDemo`、`ReceiveAndSend` 和 `ReceiveDump`。该库以 MIT License 发布，项目只调用库 API，没有复制其源文件。
- M5Stack Cardputer/ESP32 Arduino 示例中的 GPIO 初始化方式。Cardputer-Adv 的红外发射脚固定为 GPIO44。

Arduino-IRremote 建议优先使用协议编码；对未知协议、空调和厂商自定义帧使用 `sendRaw()`。因此 `/config/ir.json` 同时支持标准协议和微秒时序。RAW 配置限制为单键 160 个时序项、载波 30–60 kHz，避免无 PSRAM 的 ESP32-S3 因配置过大耗尽堆。

Cardputer-Adv 本体没有红外接收器。本项目当前是发射器和配置回放器；要做“按原遥控器自动学习”，需要外接 38 kHz 红外接收模块，并在硬件接入后再启用 IRremote 的接收 API。未接收器时不能从页面生成真实遥控码，也不会把示例码误认为通用码。

