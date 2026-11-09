#include "bootloader-common.h"

#include <stdint.h>

typedef void (*pFunction)(void);

void jump_to(uintptr_t base_address) {
  /* Read application stack pointer */
  const uintptr_t app_stack = *(volatile uintptr_t *)base_address;

  /* Read reset handler address */
  const uintptr_t app_reset_handler = *((volatile uintptr_t *)base_address + 1);
  const pFunction app_entry = (pFunction)app_reset_handler;

  /* Disable interrupts */
  __disable_irq();

  /* Stop SysTick */
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;

  /* Set main stack pointer */
  __set_MSP(app_stack);

  /* Jump to application reset handler */
  app_entry();
}

void after_jump_to(uintptr_t base_address) {
  SCB->VTOR = base_address;
  __enable_irq();
}
