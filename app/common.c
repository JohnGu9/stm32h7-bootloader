#include "app.h"

#include "FreeRTOS.h"

void null_on_dispose(void *context, const char *buffer, size_t buffer_size) {
  UNUSED(context);
  UNUSED(buffer);
  UNUSED(buffer_size);
}

void dispose_buffer(const DisposableBuffer_t *buffer) {
  buffer->on_dispose(buffer->context, buffer->data, buffer->size);
}

typedef struct {
  size_t ref_count;
  void *context;
  void (*on_dispose)(void *, const char *, size_t);
} SharedBufferContext_t;

static void dispose_shared_buffer(void *ctx, const char *data, size_t size) {
  SharedBufferContext_t *const context = (SharedBufferContext_t *)ctx;
  context->ref_count -= 1;
  if (context->ref_count == 0) {
    context->on_dispose(context->context, data, size);
    our_delete(context);
  }
}

void to_shared_disposable_buffer(DisposableBuffer_t *buffer) {
  SharedBufferContext_t *const p = our_new(SharedBufferContext_t);
  p->ref_count = 1;
  p->context = buffer->context;
  p->on_dispose = buffer->on_dispose;
  buffer->context = p;
  buffer->on_dispose = dispose_shared_buffer;
}

void copy_shared_disposable_buffer(const DisposableBuffer_t *buffer) {
  SharedBufferContext_t *p = (SharedBufferContext_t *)buffer->context;
  p->ref_count += 1;
}

int add_task(EventTaskSet_t *set, const EventTask_t *task) {
  return !insert(set, (uintptr_t)task);
}

int remove_task(EventTaskSet_t *set, const EventTask_t *task) {
  erase(set, (uintptr_t)task);
  return 0;
}

void invoke_tasks(EventTaskSet_t *set) {
  for_each(set, el) {
    const EventTask_t *task = (const EventTask_t *)(*el);
    task->callback(task->context);
  }
}

void *our_realloc(void *ptr, size_t size) {
  void *const new_ptr = pvPortMalloc(size);
  if (new_ptr == NULL) {
    return NULL;
  }
  if (ptr != NULL) {
    memcpy(new_ptr, ptr, size);
    vPortFree(ptr);
  }

  return new_ptr;
}

void our_free(void *ptr) { vPortFree(ptr); }
