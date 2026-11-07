#include "app.h"
#include "http-server.h"
#include "lwip/err.h"
#include "lwip/tcp.h"
#include <assert.h>

static size_t get_tcp_transfer_space(void *context) {
  ClientContext_t *const client_context = (ClientContext_t *)context;
  return tcp_sndbuf(client_context->pcb);
}

static void tcp_transfer(void *context, const char *data, size_t size) {
  ClientContext_t *const client_context = (ClientContext_t *)context;
  const int is_queue_empty =
      transfer_queue_is_empty(&client_context->tcp_out_queue);
  const err_t err =
      tcp_write(client_context->pcb, data, size,
                is_queue_empty ? 0 : TCP_WRITE_FLAG_MORE /*NO COPY*/);
  assert(err == ERR_OK);
  if (is_queue_empty || tcp_sndbuf(client_context->pcb) <= (TCP_SND_BUF / 4)) {
    tcp_output(client_context->pcb);
  }
}

ClientContext_t *client_context_new() {
  ClientContext_t *const client_context = our_new(ClientContext_t);
  assert(client_context != NULL);
  memset(client_context, 0, sizeof(ClientContext_t));

  init(&client_context->pbuf_ref_counts);

  transfer_queue_init(&client_context->tcp_out_queue, client_context,
                      get_tcp_transfer_space, tcp_transfer);

  tiny_http_parser_init(&client_context->context, client_context,
                        on_http_header, on_http_body_stream, NULL,
                        on_http_error, NULL, NULL);
  return client_context;
}

void client_context_delete(ClientContext_t *const client_context) {
  assert(client_context != NULL);
  if (client_context->ws_upgraded) {
    tiny_http_parser_websocket_deinit(&client_context->ws_context);
  }
  tiny_http_parser_deinit(&client_context->context);

  transfer_queue_deinit(&client_context->tcp_out_queue);

  if (client_context->flash_token != 0) {
    flash_operation_queue_abort(client_context->flash_token);
  }

  for_each(&client_context->pbuf_ref_counts, key, el) {
    struct pbuf *const p = (struct pbuf *)(*key);
    tcp_recved(client_context->pcb, p->tot_len);
    pbuf_free(p);
  }
  cleanup(&client_context->pbuf_ref_counts);

  our_delete(client_context);
}

void client_context_send(ClientContext_t *client_context,
                         const DisposableBuffer_t *buffer) {
  transfer_queue_send(&client_context->tcp_out_queue, buffer);
}

void client_context_sent(ClientContext_t *client_context, size_t sent_size) {
  transfer_queue_sent(&client_context->tcp_out_queue, sent_size);
}

void client_context_recv(ClientContext_t *client_context, struct pbuf *p) {
  client_context->current_pbuf = p;
  insert(&client_context->pbuf_ref_counts, (uintptr_t)p, 1);
  // traverse all pbuf
  for (struct pbuf *q = p; q != NULL; q = q->next) {
    if (client_context->custom_error == NULL) {
      tiny_http_parser_input(&client_context->context, (const char *)q->payload,
                             q->len);
    }
  }
  client_context->current_pbuf = NULL;
  client_context_release(client_context, p);
}

void client_context_require(ClientContext_t *client_context, struct pbuf *p) {
  size_t *ref_count = get(&client_context->pbuf_ref_counts, (uintptr_t)p);
  assert(ref_count != NULL);
  *ref_count += 1;
}

void client_context_release(ClientContext_t *client_context, struct pbuf *p) {
  size_t *ref_count = get(&client_context->pbuf_ref_counts, (uintptr_t)p);
  assert(ref_count != NULL);
  *ref_count -= 1;
  if (*ref_count == 0) {
    erase(&client_context->pbuf_ref_counts, (uintptr_t)p);
    tcp_recved(client_context->pcb, p->tot_len);
    pbuf_free(p);
  }
}
