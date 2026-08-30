# WCH-Link 实现

本目录承载 WCH-Link USB 命令到目标调试操作的实现，按协议、传输、调试原语、目标、会话和 USB 适配分目录组织

依赖方向从 `usb` 进入 `session`，再进入 `target`、`rvswd` 和 `transport`，底层模块不反向依赖上层命令。芯片族差异集中在 `target` 及其调用的专用 backend，通用模块不保存芯片寄存器表

目录下的 README 说明各模块职责和入口，具体协议字段、目标参数和实验结论以源码与项目级资料为准
