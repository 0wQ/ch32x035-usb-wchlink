# 目标

本目录是目标适配的 seam，集中目标身份读取、ChipID 到 module 的解析、target ports、loader contract 和目标族专用行为。`rvswd_target_registry.c/.h` 只负责 module 索引与分派，每个 `rvswd_target_xxx.c/.h` 持有对应族的 profile、目标操作和错误语义

profile 只保存稳定的目标数据和能力，寄存器访问顺序、前置配置、错误恢复和 loader 执行时序由对应族模块的 C 函数实现。CH32X03X、CH32L103、CH32V20X 按官方抓包独立实现，CH32V30X、CH58X、CH59X 的未验证行为以带 TODO 的占位实现接管，不通过公共默认回退隐藏。CH32V20X 已确认 CH32V203 的 ChipID、ESIG、direct-memory loader 下载、RCC 前置、loader ABI、全擦和基础 reset 路径；Option Bytes 写入和内存分配仍保持未支持。模块可以绑定无族选择的共享事务 backend，但不能让 backend 反向识别族

target ports 在连接成功后锁定 module，并向 session 提供稳定的语义接口。USB、transport 和通用 RVSWD 只依赖中立类型与原语，不读取族模块的私有数据
