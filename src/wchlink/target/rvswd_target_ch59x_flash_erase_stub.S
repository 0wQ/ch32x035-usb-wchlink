    .section .rodata.rvswd_target_ch59x_flash_erase_stub,"a",@progbits
    .balign 4
    .global rvswd_target_ch59x_flash_erase_stub_start
    .global rvswd_target_ch59x_flash_erase_stub_end
    .type rvswd_target_ch59x_flash_erase_stub_start, @object

rvswd_target_ch59x_flash_erase_stub_start:
    /* a1 保存当前块地址，a2 保存官方 WCH-LinkE 使用的结束地址 */
    // TODO: 当前复制 CH58X 的既有 4 KiB 擦除流程，待 CH59X 官方抓包复核
    csrr    t6, mstatus
    andi    t5, t6, -137
    csrw    mstatus, t5
    li      a3, 0x40001800
    li      t0, 5

    li      t1, 4
    sb      t1, 6(a3)
    sb      zero, 6(a3)
    sb      t0, 6(a3)
    li      t2, 0xff
    sb      t2, 4(a3)
.Lch59x_wait_open_first:
    lb      t1, 6(a3)
    blt     t1, zero, .Lch59x_wait_open_first
    sb      t2, 4(a3)
.Lch59x_wait_open_second:
    lb      t1, 6(a3)
    blt     t1, zero, .Lch59x_wait_open_second
    sb      zero, 6(a3)

.Lch59x_erase_block:
    li      t4, 0x20
    li      t5, 0x1000
    sb      zero, 6(a3)
    sb      t0, 6(a3)
    li      t1, 6
    sb      t1, 4(a3)
.Lch59x_wait_address_command:
    lb      t1, 6(a3)
    blt     t1, zero, .Lch59x_wait_address_command
    sb      zero, 6(a3)

    sb      zero, 6(a3)
    sb      t0, 6(a3)
    sb      t4, 4(a3)
    mv      t1, a1
    li      t2, 3
.Lch59x_write_address:
    lb      t3, 6(a3)
    blt     t3, zero, .Lch59x_write_address
    srli    t3, t1, 16
    sb      t3, 4(a3)
    slli    t1, t1, 8
    addi    t2, t2, -1
    bnez    t2, .Lch59x_write_address

.Lch59x_finish_erase_command:
    lb      t1, 6(a3)
    blt     t1, zero, .Lch59x_finish_erase_command
    sb      zero, 6(a3)

.Lch59x_poll_status:
    sb      zero, 6(a3)
    sb      t0, 6(a3)
    sb      t0, 4(a3)
    li      t2, 2
.Lch59x_read_status:
    lb      t3, 6(a3)
    blt     t3, zero, .Lch59x_read_status
    lbu     t1, 4(a3)
    addi    t2, t2, -1
    bnez    t2, .Lch59x_read_status
.Lch59x_finish_status_command:
    lb      t3, 6(a3)
    blt     t3, zero, .Lch59x_finish_status_command
    sb      zero, 6(a3)
    andi    t1, t1, 1
    bnez    t1, .Lch59x_poll_status

    add     a1, a1, t5
    bltu    a1, a2, .Lch59x_erase_block
    csrw    mstatus, t6
    li      a0, 0
    ebreak

    .balign 4
rvswd_target_ch59x_flash_erase_stub_end:
    .size rvswd_target_ch59x_flash_erase_stub_start, rvswd_target_ch59x_flash_erase_stub_end - rvswd_target_ch59x_flash_erase_stub_start
