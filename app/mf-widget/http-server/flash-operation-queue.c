#include "app.h"
#include "http-server.h"
#include <assert.h>

static TransferQueue_t _flash_write_queue;
static uint8_t _flash_write_buffer[FLASH_WORD_SIZE];
static size_t _write_offset = 0;
static size_t _write_size = 0;
static size_t _written_size = 0;
static EventTask_t _on_flash_status_completed;

enum {
  _init_clear, // can't write
  _writing,    // ok to write
  _complete,   // write may be incomplete
  _completed,  // write is completed
  _abort,
};
static int _flash_status = _complete;

static void try_completed(void *context) {
  assert(_flash_status == _complete);
  if (!transfer_queue_is_empty(&_flash_write_queue) || is_flash_busy()) {
    return;
  }
  const size_t token = (size_t)context;
  int res = release_flash_token(token);
  assert(res == HAL_OK);
  transfer_queue_deinit(&_flash_write_queue);
  memset(&_flash_write_queue, 0, sizeof(_flash_write_queue));
  _flash_status = _completed;
  _on_flash_status_completed.callback(_on_flash_status_completed.context);
}

static size_t get_available_transfer_size(void *context) {
  return sizeof(_flash_write_buffer) - _write_offset;
}

static void on_flash_write_completed(void *context) {
  if (_flash_status == _abort) {
    return;
  }
  assert(_flash_status != _completed);

  const size_t sent_size = _write_offset;
  _write_offset = 0;
  transfer_queue_sent(&_flash_write_queue, sent_size);
  if (_flash_status == _complete) {
    try_completed(context);
  }
}

static void transfer(void *context, const char *data, size_t size) {
  if (unlikely(_flash_status == _init_clear)) {
    return;
  }
  const size_t token = (size_t)context;

  memcpy(_flash_write_buffer + _write_offset, data, size);
  _write_offset += size;

  _written_size += size;
  assert(_written_size <= _write_size);

  if (_write_offset < sizeof(_flash_write_buffer)) { // only write when buffer is fully filled
    if (_flash_status == _complete) {
      assert(!transfer_queue_is_empty(&_flash_write_queue));
    }
    return;
  }

  static EventTask_t on_completed = {
      .callback = on_flash_write_completed,
  };
  on_completed.context = context;
  int res = flash_write_async(token, _flash_write_buffer, &on_completed);
  assert(res == HAL_OK);
}

static void on_flash_clear_completed(void *context) {
  if (_flash_status == _abort) {
    return;
  }
  assert(_flash_status != _completed);

  _write_offset = 0;
  if (_flash_status == _complete) {
    transfer_queue_sent(&_flash_write_queue, 0);
    try_completed(context);
    return;
  }
  _flash_status = _writing;
  transfer_queue_sent(&_flash_write_queue, 0);
}

void flash_operation_queue_init(size_t token) {
  transfer_queue_init(&_flash_write_queue, (void *)token, get_available_transfer_size,
                      transfer);
  // fake filled the buffer, prevent write before clear completed
  _write_offset = sizeof(_flash_write_buffer);

  _write_size = 0;
  _written_size = 0;
  _flash_status = _init_clear;

  static EventTask_t on_completed = {
      .callback = on_flash_clear_completed,
  };
  on_completed.context = (void *)token;
  int res = flash_app_clear_async(token, &on_completed);
  assert(res == HAL_OK);
}

void flash_operation_queue_write(size_t token,
                                 const DisposableBuffer_t *buffer) {
  assert(_flash_status == _init_clear || _flash_status == _writing);
  _write_size += buffer->size;
  assert(_write_size >= buffer->size);
  transfer_queue_send(&_flash_write_queue, buffer);
}

void flash_operation_queue_complete(size_t token,
                                    const EventTask_t *on_completed) {
  assert(_flash_status == _init_clear || _flash_status == _writing);
  _on_flash_status_completed = *on_completed;
  const size_t mod = _write_size % sizeof(_flash_write_buffer);
  if (mod != 0) {
    static const char empty_buffer[sizeof(_flash_write_buffer)] = {0};
    const DisposableBuffer_t write_buffer =
        StaticDisposableBuffer(empty_buffer, (sizeof(_flash_write_buffer) - mod));
    flash_operation_queue_write(token, &write_buffer);
    _flash_status = _complete;
    return;
  }
  _flash_status = _complete;
  try_completed(_flash_write_queue.context);
}

void flash_operation_queue_abort(size_t token) {
  _flash_status = _abort;

  abort_flash_token(token);
  transfer_queue_deinit(&_flash_write_queue);
  memset(&_flash_write_queue, 0, sizeof(_flash_write_queue));
}

size_t flash_operation_queue_get_write_size() { return _write_size; }
