#ifndef BOOTLOADER_COMMON_CONFIG_H
#define BOOTLOADER_COMMON_CONFIG_H

// @TODO: remember update ld script

#define BL_START_ADDR   0x08000000    // (copy the number to bootloader ld script FLASH section)
#define APP_START_ADDR  0x08100000    // (copy the number to application ld script FLASH section)
#define APP_SIZE        (1024 * 1024) // 1MB

#include "stm32h7xx_hal.h"

#endif
