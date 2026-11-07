#include "app.h"
#include "http-server.h"
#include <assert.h>
#include <stdint.h>

static TransferQueue_t queue;
static uint8_t buffer[FLASH_WORD_SIZE];
static size_t offset = 0;
static size_t write_size = 0;
static size_t written_size = 0;
static EventTask_t on_flash_status_completed;

enum {
  _init_clear, // can't write
  _writing,    // ok to write
  _complete,   // write may be incomplete
  _completed,  // write is completed
  _abort,
};
static int flash_status = _complete;

static void try_completed(void *context) {
  assert(flash_status == _complete);
  if (!transfer_queue_is_empty(&queue) || is_flash_busy()) {
    return;
  }
  const size_t token = (size_t)context;
  int res = release_flash_token(token);
  assert(res == HAL_OK);
  transfer_queue_deinit(&queue);
  memset(&queue, 0, sizeof(queue));
  flash_status = _completed;
  on_flash_status_completed.callback(on_flash_status_completed.context);
}

static size_t get_available_transfer_size(void *context) {
  return sizeof(buffer) - offset;
}

static void on_flash_write_completed(void *context) {
  if (flash_status == _abort) {
    return;
  }
  assert(flash_status != _completed);

  const size_t sent_size = offset;
  offset = 0;
  transfer_queue_sent(&queue, sent_size);
  if (flash_status == _complete) {
    try_completed(context);
  }
}

static void transfer(void *context, const char *data, size_t size) {
  const size_t token = (size_t)context;

  memcpy(buffer + offset, data, size);
  offset += size;

  written_size += size;
  assert(written_size <= write_size);

  if (offset < sizeof(buffer)) { // only write when buffer is fully filled
    if (flash_status == _complete) {
      assert(!transfer_queue_is_empty(&queue));
    }
    return;
  }

  static EventTask_t on_completed = {
      .callback = on_flash_write_completed,
  };
  on_completed.context = context;
  int res = flash_app_write_async(token, buffer, &on_completed);
  assert(res == HAL_OK);
}

static void on_flash_clear_completed(void *context) {
  if (flash_status == _abort) {
    return;
  }
  assert(flash_status != _completed);

  offset = 0;
  if (flash_status == _complete) {
    transfer_queue_sent(&queue, 0);
    try_completed(context);
    return;
  }
  flash_status = _writing;
  transfer_queue_sent(&queue, 0);
}

void flash_operation_queue_init(size_t token) {
  transfer_queue_init(&queue, (void *)token, get_available_transfer_size,
                      transfer);
  // fake filled the buffer, prevent write before clear completed
  offset = sizeof(buffer);

  write_size = 0;
  written_size = 0;
  flash_status = _init_clear;

  static EventTask_t on_completed = {
      .callback = on_flash_clear_completed,
  };
  on_completed.context = (void *)token;
  int res = flash_app_clear_async(token, &on_completed);
  assert(res == HAL_OK);
}

void flash_operation_queue_write(size_t token,
                                 const DisposableBuffer_t *buffer) {
  assert(flash_status == _init_clear || flash_status == _writing);
  write_size += buffer->size;
  assert(write_size >= buffer->size);
  transfer_queue_send(&queue, buffer);
}

void flash_operation_queue_complete(size_t token,
                                    const EventTask_t *on_completed) {
  assert(flash_status == _init_clear || flash_status == _writing);
  on_flash_status_completed = *on_completed;
  flash_status = _complete;
  const size_t mod = write_size % sizeof(buffer);
  if (mod != 0) {
    static const char empty_buffer[sizeof(buffer)] = {0};
    const DisposableBuffer_t write_buffer =
        StaticDisposableBuffer(empty_buffer, (sizeof(buffer) - mod));
    flash_operation_queue_write(token, &write_buffer);
    return;
  }
  try_completed(queue.context);
}

void flash_operation_queue_abort(size_t token) {
  flash_status = _abort;

  abort_flash_token(token);
  transfer_queue_deinit(&queue);
  memset(&queue, 0, sizeof(queue));
}

size_t flash_operation_queue_get_write_size() { return write_size; }
