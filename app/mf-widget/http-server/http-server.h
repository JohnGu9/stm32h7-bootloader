#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "mf-widget/widgets.h"

#include "mf.h"
#include "tiny-http-parser.h"
#include "websocket-accept.h"

#include <lwip/err.h>
#include <lwip/tcp.h>

#include <stdint.h>

typedef struct {
  mContext_t *context;
  struct tcp_pcb *pcb;
  map(uintptr_t,
      uintptr_t) /* <struct tcp_pcb *, ClientContext_t *> */ active_client_pcb;
} HttpServerWidgetState_t;

void close_pcb(struct tcp_pcb *pcb, HttpServerWidgetState_t *const state);

// transfer-queue.c

typedef struct {
  DisposableBuffer_t buffer;
  size_t offset;
  size_t confirm_sent;
} TransferQueueElement_t;

typedef struct {
  list(TransferQueueElement_t) transfer_queue;
  void *context;
  size_t (*get_available_transfer_size)(void *context);
  void (*transfer)(void *context, const char *data, size_t size);
} TransferQueue_t;

void transfer_queue_init(TransferQueue_t *queue, void *context,
                         size_t (*get_available_transfer_size)(void *context),
                         void (*transfer)(void *context, const char *data,
                                          size_t size));
void transfer_queue_send(TransferQueue_t *queue,
                         const DisposableBuffer_t *buffer);
void transfer_queue_sent(TransferQueue_t *queue, size_t sent_size);
void transfer_queue_deinit(TransferQueue_t *queue);

int transfer_queue_is_empty(const TransferQueue_t *queue);
size_t transfer_queue_get_size(const TransferQueue_t *queue);

// http.c

size_t on_http_header(TinyHttpParserContext_t *context,
                      const TinyHttpParserHeader_t *header);

size_t on_http_body_stream(TinyHttpParserContext_t *context, const char *data,
                           size_t size, size_t *remain_not_recv_size);

void on_http_error(TinyHttpParserContext_t *context, const char *error_reason);

// client_context.c

typedef struct {
  HttpServerWidgetState_t *state;
  const char *custom_error;
  struct tcp_pcb *pcb;
  struct pbuf *current_pbuf;
  size_t flash_token;
  map(uintptr_t, size_t) /* map<struct tcp_pcb *, size_t> */ pbuf_ref_counts;
  TransferQueue_t tcp_out_queue;
  TinyHttpParserContext_t context;
  int ws_upgraded;
  TinyHttpParserWebsocketContext_t ws_context;
  char ws_accept_key[WEBSOCKET_ACCEPT_KEY_SIZE];
} ClientContext_t;

ClientContext_t *client_context_new();

void client_context_delete(ClientContext_t *const client_context);

void client_context_send(ClientContext_t *client_context,
                         const DisposableBuffer_t *buffer);

#define client_context_send_literal(client_context, literal)                   \
  do {                                                                         \
    const DisposableBuffer_t __disposable_buffer =                             \
        LiteralDisposableBuffer(literal);                                      \
    client_context_send(client_context, &__disposable_buffer);                 \
  } while (0)

#define client_context_send_static(client_context, array)                      \
  do {                                                                         \
    const DisposableBuffer_t __disposable_buffer =                             \
        StaticDisposableBuffer((const char *)(array), sizeof(array));          \
    client_context_send(client_context, &__disposable_buffer);                 \
  } while (0)

void client_context_sent(ClientContext_t *client_context, size_t sent_size);

void client_context_recv(ClientContext_t *client_context, struct pbuf *p);

void client_context_require(ClientContext_t *client_context, struct pbuf *p);

void client_context_release(ClientContext_t *client_context, struct pbuf *p);

// websocket.c

const TinyHttpParserStringView_t *
get_ws_key(const TinyHttpParserRequestHeader_t *request_header);

void on_ws_message_or_control(TinyHttpParserWebsocketContext_t *context,
                              uint8_t opcode, const char *data, size_t size);

void on_ws_error(TinyHttpParserWebsocketContext_t *context, const char *reason);

// flash.c

size_t get_flash_token();

int is_valid_flash_token(size_t token);

int is_flash_busy();
int is_flash_has_error();

int flash_app_clear(size_t token);
int flash_app_clear_async(size_t token, const EventTask_t *on_completed);

#define FLASH_WORD_SIZE (FLASH_NB_32BITWORD_IN_FLASHWORD * sizeof(uint32_t))
int flash_write(size_t token, const uint8_t flash_word[FLASH_WORD_SIZE]);
int flash_write_async(size_t token, const uint8_t flash_word[FLASH_WORD_SIZE],
                      const EventTask_t *on_completed);

int release_flash_token(size_t token);

int abort_flash_token(size_t token);

// flash-operation-queue.c

void flash_operation_queue_init(size_t token);

void flash_operation_queue_write(size_t token,
                                 const DisposableBuffer_t *buffer);

void flash_operation_queue_complete(size_t token,
                                    const EventTask_t *on_completed);

void flash_operation_queue_abort(size_t token);

size_t flash_operation_queue_get_write_size();

#endif
