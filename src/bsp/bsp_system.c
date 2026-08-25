#include "bsp/bsp_system.h"

#include "ch32x035_flash.h"

// 进入 ISP 前先屏蔽中断，再切换下次复位启动源，避免运行中的外设继续改写硬件状态
void bsp_system_enter_isp(void) {
    __disable_irq();
    SystemReset_StartMode(Start_Mode_BOOT);
    NVIC_SystemReset();

    while (1) {
    }
}
