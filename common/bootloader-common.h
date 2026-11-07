#ifndef BOOTLOADER_COMMON_H
#define BOOTLOADER_COMMON_H

#include "bootloader-common-config.h"

void jump_to(uintptr_t base_address);

void after_jump_to(uintptr_t base_address);

/**
 * @brief bootloader call
 *
 */
#define jump_to_application() jump_to(APP_START_ADDR)

/**
 * @brief application call after `HAL_Init()`
 *
 */
#define after_jump_to_application() after_jump_to(APP_START_ADDR)
#define after_jump_to_bootloader() after_jump_to(BL_START_ADDR)

#endif
