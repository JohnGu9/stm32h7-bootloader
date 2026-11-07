#include "app.h"
#include "http-server/http-server.h"
#include "widgets.h"

#include "mf.h"
#include "tiny-http-parser.h"
#include "websocket-accept.h"

#include <assert.h>
#include <lwip/err.h>
#include <lwip/tcp.h>
#include <stdint.h>

void close_pcb(struct tcp_pcb *pcb, HttpServerWidgetState_t *const state) {
  tcp_recv(pcb, NULL);
  tcp_sent(pcb, NULL);

  ClientContext_t *const client_context =
      (ClientContext_t *)(*get(&state->active_client_pcb, (uintptr_t)pcb));
  client_context_delete(client_context);
  erase(&state->active_client_pcb, (uintptr_t)pcb);

  const err_t status = tcp_close(pcb);
  if (unlikely(status != ERR_OK)) {
    tcp_abort(pcb);
  }
  tcp_output(pcb);
}

static err_t on_recv(void *ctx, struct tcp_pcb *pcb, struct pbuf *p,
                     err_t err) {
  ClientContext_t *const client_context = (ClientContext_t *)ctx;
  assert(pcb != NULL);
  if (p != NULL && p->tot_len != 0) {
    client_context_recv(client_context, p);
    if (client_context->custom_error != NULL) {
      close_pcb(pcb, client_context->state);
    }
  } else { // FIN
    close_pcb(pcb, client_context->state);
  }
  return ERR_OK;
}

static err_t on_sent(void *ctx, struct tcp_pcb *pcb, u16_t len) {
  ClientContext_t *const client_context = (ClientContext_t *)ctx;
  client_context_sent(client_context, len);
  return ERR_OK;
}

static err_t on_accept(void *ctx, struct tcp_pcb *pcb, err_t err) {
  assert(pcb != NULL);
  HttpServerWidgetState_t *const state = (HttpServerWidgetState_t *)ctx;
  if (likely(err == ERR_OK)) {
    ClientContext_t *const client_context = client_context_new();
    client_context->state = state;
    client_context->pcb = pcb;
    insert(&state->active_client_pcb, (uintptr_t)pcb,
           (uintptr_t)client_context);
    tcp_arg(pcb, client_context);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
  } else {
    tcp_abort(pcb);
  }
  return ERR_OK;
}

static void dispose_pv_buffer(void *, const char *data, size_t) {
  our_free((void *)data);
}

// broadcast message to all ws clients
static void ws_print(void *context, const DisposableBuffer_t *buffer) {
  HttpServerWidgetState_t *const state = (HttpServerWidgetState_t *)context;

  char *const header_buffer =
      our_realloc(NULL, 10); // server no mask, head max size = 10
  const size_t header_size = tiny_http_parser_websocket_set_data_frame_header(
      header_buffer, 1, 1, buffer->size, NULL);
  DisposableBuffer_t head_buffer = {
      .data = header_buffer,
      .size = header_size,
      .on_dispose = dispose_pv_buffer,
  };
  to_shared_disposable_buffer(&head_buffer);

  DisposableBuffer_t shared_buffer = *buffer;
  to_shared_disposable_buffer(&shared_buffer);

  for_each(&state->active_client_pcb, key, el) {
    ClientContext_t *const client_context = (ClientContext_t *)*el;
    if (client_context->ws_upgraded) {
      copy_shared_disposable_buffer(&head_buffer);
      client_context_send(client_context, &head_buffer);
      copy_shared_disposable_buffer(&shared_buffer);
      client_context_send(client_context, &shared_buffer);
    }
  }

  dispose_buffer(&head_buffer);
  dispose_buffer(&shared_buffer);
}

static void *tiny_http_realloc(void *ptr, size_t size) {
  if (size > 1024) {
    return NULL;
  }
  return our_realloc(ptr, size);
}

static void *init_state(mContext_t *context) {
  HttpServerWidgetState_t *const state = our_new(HttpServerWidgetState_t);

  tiny_http_parser_hook_init(tiny_http_realloc, our_free);

  state->context = context;
  init(&state->active_client_pcb);

  struct tcp_pcb *pcb = tcp_new();
  assert(pcb != NULL);
  err_t err = tcp_bind(pcb, IP_ADDR_ANY, 80);
  assert(err == ERR_OK);
  pcb = tcp_listen(pcb);
  assert(pcb != NULL);
  tcp_arg(pcb, state);
  tcp_accept(pcb, on_accept);
  state->pcb = pcb;

  const Print_t my_print = {
      .context = state,
      .print = ws_print,
  };
  set_mf_print(context, &my_print);

  return state;
}

static void build(mContext_t *context, mWidget_t children[MF_MAX_CHILDREN]) {
  return;
}

static void dispose(mContext_t *context) {
  m_get_state_cast(state, context, HttpServerWidgetState_t);

  reset_mf_print(context);

  for_each(&state->active_client_pcb, key, el) {
    tcp_abort((struct tcp_pcb *)(*key));
    ClientContext_t *const client_context = (ClientContext_t *)*el;
    client_context_delete(client_context);
  }
  cleanup(&state->active_client_pcb);

  // a listened tcp pcb only can call tcp_close, not tcp_abort
  tcp_close(state->pcb);

  our_delete(state);
  return;
};

const mWidgetClass_t HttpServerWidgetClass = {
    .init_state = &init_state,
    .build = &build,
    .should_rebuild = m_always_rebuild,
    .dispose = &dispose,
};
