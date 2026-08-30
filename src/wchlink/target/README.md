# 目标

本目录是目标适配的 seam，集中目标身份读取、ChipID 到 profile 的解析、target ports、loader contract 和目标族专用执行行为。`rvswd_target_registry.c/.h` 只负责已迁移族模块分派，`rvswd_target_x03x.c/.h` 集中 CH32X03X 的数据与 loader 行为，`rvswd_target_profile.c/.h` 暂存未迁移族 profile

profile 只保存稳定的目标数据和能力，寄存器访问顺序、前置配置、错误恢复和 loader 执行时序由 C 函数实现。未确认的目标族沿用现有 profile 和默认行为，不在这里提前创建空适配文件。loader prepare 是明确的小接口，CH32X03X 执行真实前置序列，其他族为空实现

session 通过 target ports 使用本目录，USB、transport 和通用 RVSWD 不直接读取目标 profile 的私有数据
