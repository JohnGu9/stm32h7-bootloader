#include "bootloader-common.h"

#include <stdint.h>

typedef void (*pFunction)(void);

void jump_to(uintptr_t base_address) {
  /* Read application stack pointer */
  uintptr_t appStack = *(volatile uintptr_t *)base_address;

  /* Read reset handler address */
  uintptr_t appResetHandler = *((volatile uintptr_t *)base_address + 1);
  pFunction appEntry = (pFunction)appResetHandler;

  /* Disable interrupts */
  __disable_irq();

  /* Stop SysTick */
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;

  /* Set main stack pointer */
  __set_MSP(appStack);

  /* Jump to application reset handler */
  appEntry();
}

void after_jump_to(uintptr_t base_address) {
  SCB->VTOR = base_address; // Add This
  __enable_irq();           // Add This
}
