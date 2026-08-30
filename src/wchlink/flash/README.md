# Flash

本目录实现目标 Code Flash 擦除、编程、校验、Option Bytes 和不同目标族的 Flash backend，向 target ports 提供目标操作结果

通用 Flash facade 负责选择 backend 和映射结果，芯片专用控制寄存器、解锁顺序和 loader 约束留在对应实现中。这里不处理 USB 端点生命周期，也不直接解释上层 session 的命令格式

验证入口是 command transfer fixture、memory fixture 以及目标板上的擦除、编程和严格回读
