    .section .rodata.ch5xx_flash_erase_stub,"a",@progbits
    .balign 4
    .global ch5xx_flash_erase_stub_start
    .global ch5xx_flash_erase_stub_end
    .type ch5xx_flash_erase_stub_start, @object

ch5xx_flash_erase_stub_start:
    /* a1 保存当前块地址，a2 保存官方 LinkE 使用的结束地址 */
    // 擦除命令跨越整个 Flash，执行期间禁止目标中断打断 RAM stub
    csrr    t6, mstatus
    andi    t5, t6, -137
    csrw    mstatus, t5
    li      a3, 0x40001800
    li      t0, 5

    // 先打开 CH5xx Flash 命令口，避免上一次会话残留的控制状态影响首个扇区
    li      t1, 4
    sb      t1, 6(a3)
    sb      zero, 6(a3)
    sb      t0, 6(a3)
    li      t2, 0xff
    sb      t2, 4(a3)
.Lwait_open_first:
    lb      t1, 6(a3)
    blt     t1, zero, .Lwait_open_first
    sb      t2, 4(a3)
.Lwait_open_second:
    lb      t1, 6(a3)
    blt     t1, zero, .Lwait_open_second
    sb      zero, 6(a3)

.Lerase_block:
    /* CH58x 和 CH59x 的全擦路径都按 LinkE 使用 4 KiB 扇区命令 */
    li      t4, 0x20
    li      t5, 0x1000

    /* 先发送地址模式命令 0x06，再发送当前擦除命令 */
    sb      zero, 6(a3)
    sb      t0, 6(a3)
    li      t1, 6
    sb      t1, 4(a3)
.Lwait_address_command:
    lb      t1, 6(a3)
    blt     t1, zero, .Lwait_address_command
    sb      zero, 6(a3)

    sb      zero, 6(a3)
    sb      t0, 6(a3)
    sb      t4, 4(a3)

    /* Flash 命令口按高字节优先接收 24 位块地址 */
    mv      t1, a1
    li      t2, 3
.Lwrite_address:
    lb      t3, 6(a3)
    blt     t3, zero, .Lwrite_address
    srli    t3, t1, 16
    sb      t3, 4(a3)
    slli    t1, t1, 8
    addi    t2, t2, -1
    bnez    t2, .Lwrite_address

.Lfinish_erase_command:
    lb      t1, 6(a3)
    blt     t1, zero, .Lfinish_erase_command
    sb      zero, 6(a3)

    /* 与 LinkE 一致连续读取两次状态，使用第二次的 bit 0 判断完成 */
.Lpoll_status:
    sb      zero, 6(a3)
    sb      t0, 6(a3)
    sb      t0, 4(a3)
    li      t2, 2
.Lread_status:
    lb      t3, 6(a3)
    blt     t3, zero, .Lread_status
    lbu     t1, 4(a3)
    addi    t2, t2, -1
    bnez    t2, .Lread_status
.Lfinish_status_command:
    lb      t3, 6(a3)
    blt     t3, zero, .Lfinish_status_command
    sb      zero, 6(a3)
    andi    t1, t1, 1
    bnez    t1, .Lpoll_status

    add     a1, a1, t5
    bltu    a1, a2, .Lerase_block

    // 在进入调试断点前恢复执行前的中断状态
    csrw    mstatus, t6
    li      a0, 0
    ebreak

    .balign 4
ch5xx_flash_erase_stub_end:
    .size ch5xx_flash_erase_stub_start, ch5xx_flash_erase_stub_end - ch5xx_flash_erase_stub_start
