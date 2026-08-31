# Flash

本目录保留无芯片族选择的 Flash transaction backend 和 CH58X/CH59X 共享擦除 stub。CH32 控制器和 CH58X/CH59X 命令口只实现实际可复用的事务细节，目标 module 负责绑定对应 profile、能力、loader execute 和 backend，向 target ports 返回统一目标结果

本目录不按 family 选择 backend，不处理 USB 端点生命周期，也不解释上层 session 的命令格式。芯片专用控制寄存器、解锁顺序、loader 约束、错误恢复和保护策略由对应 module 绑定或传入，不通过公共 dispatcher 再次选择

验证入口是 command transfer fixture、memory fixture 以及目标板上的擦除、编程和严格回读
