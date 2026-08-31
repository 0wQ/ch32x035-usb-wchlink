# RVSWD

本目录提供 DMI 操作、Debug Module、abstract command、目标内存访问、复位和一次操作的错误状态管理，供 target 和 session 的目标端口调用

这里保存通用 RISC-V 调试原语、状态轮询与恢复逻辑，不保存芯片族识别表、Flash 控制器参数或 USB 命令。目标差异由 `target/rvswd_target_xxx.c` 组织，不由调用者拼接寄存器访问序列

验证入口是 debug、resume、memory 和 execute-prepare fixture
