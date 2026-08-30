# 会话

本目录管理 WCH-Link command 分派、目标控制事务、Flash 数据阶段、loader 生命周期和 USB data IN/OUT 的待处理状态

session 负责把 wire command 组织成有序目标操作，并将目标错误映射为上位机可见结果。目标寄存器和 RVSWD 帧由 target ports 与下层模块处理，session 不实现物理传输

验证入口是 command transfer fixture、wire transfer fixture 和 USB 主循环回归
