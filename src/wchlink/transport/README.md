# 传输

本目录实现 RVSWD 帧编码、GPIO 物理收发和 DMI transport，负责时钟、方向切换、回复解析、重试和传输错误状态

transport 只提供按地址读写的调试传输接口，不保存芯片族 Flash 参数，也不处理 USB 命令。物理层通过 BSP 和 SDK 使用实际引脚，目标层通过 transport 访问 DMI

验证入口是 wire fixture、transport fixture 和连接后的目标状态命令
