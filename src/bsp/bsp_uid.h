#pragma once

#include <stdint.h>

#define CHIP_UID_1 ((uint32_t)(*((volatile uint32_t *)(0x1FFFF7E8u))))
#define CHIP_UID_2 ((uint32_t)(*((volatile uint32_t *)(0x1FFFF7ECu))))
#define CHIP_UID_3 ((uint32_t)(*((volatile uint32_t *)(0x1FFFF7F0u))))

#define BSP_UID_HEX16_STR_LEN 17u

uint32_t bsp_get_chip_id(void);
void bsp_get_uid_hex16(char out_hex[BSP_UID_HEX16_STR_LEN]);
