#include "http-server.h"
#include <assert.h>

void transfer_queue_init(TransferQueue_t *queue, void *context,
                         size_t (*get_available_transfer_size)(void *context),
                         void (*transfer)(void *context, const char *data,
                                          size_t size)) {
  init(&queue->transfer_queue);
  queue->context = context;
  queue->get_available_transfer_size = get_available_transfer_size;
  queue->transfer = transfer;
}

void transfer_queue_send(TransferQueue_t *queue,
                         const DisposableBuffer_t *buffer) {
  const TransferQueueElement_t el = {
      .buffer = *buffer,
      .offset = 0,
      .confirm_sent = 0,
  };
  if (!push(&queue->transfer_queue, el)) {
    // @TODO: error handle
    dispose_buffer(buffer);
    return;
  }
  transfer_queue_sent(queue, 0);
}

#define MIN(a, b) (a) < (b) ? (a) : (b)

void transfer_queue_sent(TransferQueue_t *queue, size_t sent_size) {
  TransferQueueElement_t *el = first(&queue->transfer_queue);

  // first: clean sent buffer
  for (; el != end(&queue->transfer_queue);) {
    const size_t max_confirm_sent = el->buffer.size - el->confirm_sent;
    const size_t confirm_sent = MIN(max_confirm_sent, sent_size);
    if (confirm_sent < max_confirm_sent) {
      el->confirm_sent += confirm_sent;
      // sent_size = 0;
      break;
    }
    // confirm_sent == max_confirm_sent
    sent_size -= max_confirm_sent;
    // data should all send
    assert(el->offset == el->buffer.size); 
    dispose_buffer(&el->buffer);
    el = erase(&queue->transfer_queue, el);
  }

  // second: skip to not send buffer
  for (; el != end(&queue->transfer_queue);
       el = next(&queue->transfer_queue, el)) {
    if (el->offset < el->buffer.size) {
      break;
    }
  }

  // third: send the not send buffer
  for (; el != end(&queue->transfer_queue);
       el = next(&queue->transfer_queue, el)) {

    const size_t space = queue->get_available_transfer_size(queue->context);
    const size_t remain = el->buffer.size - el->offset;
    if (remain > space) {
      if (space != 0)
        queue->transfer(queue->context, el->buffer.data + el->offset, space);
      el->offset += space;
      break;
    }

    queue->transfer(queue->context, el->buffer.data + el->offset, remain);
    el->offset = el->buffer.size; // send completed
  }
}

void transfer_queue_deinit(TransferQueue_t *queue) {
  for_each(&queue->transfer_queue, el) { dispose_buffer(&el->buffer); }
  cleanup(&queue->transfer_queue);
}

int transfer_queue_is_empty(const TransferQueue_t *queue) {
  return first(&queue->transfer_queue) == end(&queue->transfer_queue);
}

size_t transfer_queue_get_size(const TransferQueue_t *queue) {
  size_t size = 0;
  for_each(&queue->transfer_queue, el) { size++; }
  return size;
}
