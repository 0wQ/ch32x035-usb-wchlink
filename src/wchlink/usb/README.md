# USB

本目录是 CherryUSB 端点与 WCH-Link session 之间的适配层，负责枚举、端点挂载、异步完成标志、超时回收和主循环调度

USB callback 只更新端点相关状态，完整 command 和 data 阶段由主循环交给 session 处理。这里不解释 WCH-Link command 字段，不直接调用 target、RVSWD 或 Flash 实现

验证入口是设备枚举、USB command/data 端点回归和主机工具操作
