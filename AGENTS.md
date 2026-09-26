# 项目接续

开始开发前阅读 `docs/PROJECT-CONTEXT.md`；使用说明见 `README.md`，实测证据见 `docs/features-validation.md`。

- 用中文沟通。工程当前是 PlatformIO + Arduino，后续路线是 ESP-IDF + Arduino Component，尚未迁移。
- USB SD 手动共享已获用户确认完成。每次开机默认关闭，Settings → USB SD sharing → Enter 才开放介质；按当前源码保留此行为。
- 保留 Cardputer Hub N2 的 BLE 身份与配对参数、USB 键盘、帧缓冲防闪烁、退出 SSH 后保留 Wi-Fi 的行为。
- CDC on boot 会在 setup 前启动 USB；HID/MSC 必须提前注册。不要把已验收的静态注册改为 setup 内创建。
- 新的 SD 文件访问需遵循 Storage::appAccessAllowed() 与 USB 所有权约束；共享结束必须重新挂载以清除 FAT 缓存。
- 串口号每次检测；不要把 COM12 下载模式与 COM17 运行态混为一谈，不能仅凭下载模式日志判断崩溃根因。
- 复用第三方实现时保留许可证；不要将真实 Wi-Fi/SSH 密码放进受跟踪的示例配置。
- 按改动范围执行构建或已有测试，区分编译成功、用户确认与逐项实测。临时脚本、日志和本机固件备份放到被忽略的 `.diagnostics/`。
- 后续用户指令优先；不要在没有新需求时自动扩大功能范围。
