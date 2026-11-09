#ifndef APP_H
#define APP_H

#include "FreeRTOS.h"
#include "bootloader-common.h"
#include "cmsis_os2.h"

#define our_new(T) (T *)pvPortMalloc(sizeof(T))
#define our_delete(p) vPortFree(p)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define CC_REALLOC our_realloc
#define CC_FREE our_free
#include "CC/cc.h"

#define APP_EVENT_NORMAL (1U << 0)
#define APP_EVENT_EXIT (1U << 1)
#define APP_EVENT_USB (1U << 2)
#define APP_EVENT_LWIP (1U << 3)

#define APP_EVENT_ALL 0xFFFFFF

typedef struct {
  void *context;
  void (*callback)(void *context);
} EventTask_t;

typedef set(uintptr_t /* `EventTask_t *` */) EventTaskSet_t;

extern EventTaskSet_t link_state_listeners;

typedef struct {
  const char *data;
  size_t size;
  void *context;
  void (*on_dispose)(void *context, const char *buffer, size_t buffer_size);
} DisposableBuffer_t;

#define StaticDisposableBuffer(this_data, data_size)                           \
  (DisposableBuffer_t) {                                                       \
    .data = (this_data), .size = (data_size), .on_dispose = NULL,              \
  }

#define LiteralDisposableBuffer(literal)                                       \
  (DisposableBuffer_t) {                                                       \
    .data = (literal), .size = sizeof(literal) - 1, .on_dispose = NULL,        \
  }

void dispose_buffer(const DisposableBuffer_t *buffer);

void to_shared_disposable_buffer(DisposableBuffer_t *buffer);
void copy_shared_disposable_buffer(const DisposableBuffer_t *buffer);

extern osEventFlagsId_t defaultTaskEventHandle;
extern osTimerId_t usbTimerHandle;
extern osTimerId_t lwipTimerHandle;

bool is_link_state_up();

void schedule_task_on_main_thread(const EventTask_t *task);
void exit_main_thread();

void handle_otg_irq();

void *our_realloc(void *ptr, size_t size);
void our_free(void *ptr);

int add_task(EventTaskSet_t *set, const EventTask_t *task);
int remove_task(EventTaskSet_t *set, const EventTask_t *task);
void invoke_tasks(EventTaskSet_t *set);

void init_io();
void deinit_io();

#endif
