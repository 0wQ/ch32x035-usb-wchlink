# 协议

本目录定义 WCH-Link USB wire command、family、control 和请求回复的编码规则，`wchlink_wire.c` 负责无状态的帧读写与回复构造

本目录只处理线上数据格式，不解释目标芯片寄存器、Flash 算法或 RVSWD 物理时序。上层 session 使用这里的值分派命令，下层 transport 不依赖 USB wire

验证入口是主机 wire fixture 和 command transfer fixture
