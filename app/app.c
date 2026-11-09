#include "app.h"
#include "main.h"

#include "mf-widget/widgets.h"

#include "cmsis_os2.h"
#include "lwip/timeouts.h"
#include "tusb.h"
#include <assert.h>

static osMessageQueueId_t defaultTaskQueue;

static void bootloader_main();

static GPIO_PinState read_k1() {
  return HAL_GPIO_ReadPin(K1_GPIO_Port, K1_Pin);
}

static int is_k1_pressed() {
  for (size_t i = 0; i < 10; i++) {
    GPIO_PinState state = read_k1();
    if (state == GPIO_PIN_RESET) {
      // wait for button release
      do {
        osDelay(10);
      } while (read_k1() == GPIO_PIN_RESET);
      do {
        osDelay(10);
      } while (read_k1() == GPIO_PIN_RESET);
      return 1;
    }
    osDelay(100);
  }
  return 0;
}

extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
static void MX_USB_OTG_FS_PCD_Deinit(void) {
  if (HAL_PCD_DeInit(&hpcd_USB_OTG_FS) != HAL_OK) {
    Error_Handler();
  }
}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Deinit(void) {
  /* EXTI interrupt deinit*/
  HAL_NVIC_DisableIRQ(K1_EXTI_IRQn);

  /*Configure GPIO pin : K1_Pin */
  HAL_GPIO_DeInit(K1_GPIO_Port, K1_Pin);

  /*Configure GPIO pin : LED_Pin */
  HAL_GPIO_DeInit(LED_GPIO_Port, LED_Pin);

  __HAL_RCC_GPIOE_CLK_DISABLE();
  __HAL_RCC_GPIOH_CLK_DISABLE();
  __HAL_RCC_GPIOA_CLK_DISABLE();
}

void StartDefaultTask(void *argument) {
  // turn on led
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  if (is_k1_pressed()) {
    // turn off led
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);

    // enter bootloader
    bootloader_main();
  }

  MX_USB_OTG_FS_PCD_Deinit();
  MX_GPIO_Deinit();

  jump_to_application();
  return;
}

static mContext_t *new_context(void *ctx) {
  const osMemoryPoolId_t context_mem_pool = (osMemoryPoolId_t)ctx;
  return (mContext_t *)osMemoryPoolAlloc(context_mem_pool, 0U);
}

static void delete_context(void *ctx, mContext_t *context) {
  const osMemoryPoolId_t context_mem_pool = (osMemoryPoolId_t)ctx;
  osStatus_t status = osMemoryPoolFree(context_mem_pool, context);
  assert(context == NULL || status == osOK);
}

static void on_widget_error(const char *reason) {
  UNUSED(reason);
  return;
}

static void bootloader_main() {
  init_io();

  {
    const osMessageQueueAttr_t attributes = {.name = "defaultTaskQueue"};
    defaultTaskQueue = osMessageQueueNew(32, sizeof(EventTask_t), &attributes);
  }

  const osMemoryPoolId_t context_mem_pool =
      osMemoryPoolNew(8, sizeof(mContext_t), NULL);
  assert(context_mem_pool != NULL);

  mContext_t *const main_context =
      (mContext_t *)osMemoryPoolAlloc(context_mem_pool, 0);
  memset(main_context, 0, sizeof(mContext_t));
  const mWidget_t main_widget = {.class = &MainWidgetClass, .data = NULL};
  m_attach(main_context, &main_widget, context_mem_pool, &new_context,
           &delete_context, &on_widget_error);

  EventTask_t task;
  osStatus_t status;
  uint32_t flags =
      osEventFlagsWait(defaultTaskEventHandle, APP_EVENT_ALL, osFlagsWaitAny,
                       0); // clear all flags before event loop

  status = osTimerStart(lwipTimerHandle, 200);
  assert(status == osOK);

  status = osTimerStart(usbTimerHandle, 5);
  assert(status == osOK);

  for (;;) {
    flags = osEventFlagsWait(defaultTaskEventHandle, APP_EVENT_ALL,
                             osFlagsWaitAny, osWaitForever);
    if (unlikely(flags & osFlagsError)) {
      // @TODO: handle error
    } else {
      if (flags & APP_EVENT_EXIT) {
        break;
      }
      if (flags & APP_EVENT_USB) {
        tud_task();
        // ensure [tud_task] be called as max time gap 200ms
        osTimerStart(usbTimerHandle, 200);
      }
      if (flags & APP_EVENT_LWIP) {
        sys_check_timeouts();
      }
    }

    for (;;) {
      status = osMessageQueueGet(defaultTaskQueue, &task, NULL, 0);
      if (status == osErrorResource) {
        break;
      }
      if (likely(status == osOK)) {
        task.callback(task.context);
      }
      flags = osEventFlagsWait(defaultTaskEventHandle, APP_EVENT_ALL,
                               osFlagsNoClear, 0);
      if ((flags & osFlagsError) == 0 && flags != 0 &&
          flags != APP_EVENT_NORMAL) {
        break;
      }
    }
  }

  m_detach(main_context);
  status = osMemoryPoolFree(context_mem_pool, main_context);
  assert(status == osOK);
  status = osMemoryPoolDelete(context_mem_pool);
  assert(status == osOK);

  status = osMessageQueueDelete(defaultTaskQueue);
  assert(status == osOK);

  deinit_io();
}

void usbTimerCallback(void *argument) {
  UNUSED(argument);
  osEventFlagsSet(defaultTaskEventHandle, APP_EVENT_USB);
}

void lwipTimerCallback(void *argument) {
  UNUSED(argument);
  osEventFlagsSet(defaultTaskEventHandle, APP_EVENT_LWIP);
}

void schedule_task_on_main_thread(const EventTask_t *task) {
  const osStatus_t status = osMessageQueuePut(defaultTaskQueue, task, 0, 0);
  if (likely(status == osOK)) {
    osEventFlagsSet(defaultTaskEventHandle, APP_EVENT_NORMAL);
  }
}

void exit_main_thread() {
  osEventFlagsSet(defaultTaskEventHandle, APP_EVENT_EXIT);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (likely(GPIO_Pin == K1_Pin)) {
    exit_main_thread();
  }
}
