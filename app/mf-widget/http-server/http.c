#include "app.h"
#include "http-server.h"

#include "lwip/pbuf.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "bootloader-web/index.c"

typedef size_t (*router_default_fn)(TinyHttpParserContext_t *context,
                                    const TinyHttpParserHeader_t *header);

static size_t route_index(TinyHttpParserContext_t *context,
                          const TinyHttpParserHeader_t *header,
                          router_default_fn router_default) {
  ClientContext_t *const client_context =
      tiny_http_parser_get_user_context(context, ClientContext_t *);
  client_context_send_literal(client_context,
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Content-Encoding: gzip\r\n"
                              "Content-Length: " index_html_gz_len_str "\r\n"
                              "\r\n");
  client_context_send_static(client_context, index_html_gz);
  return tiny_http_parser_get_body_size(context, header);
}

static size_t route_favicon_svg(TinyHttpParserContext_t *context,
                                const TinyHttpParserHeader_t *header,
                                router_default_fn router_default) {
  ClientContext_t *const client_context =
      tiny_http_parser_get_user_context(context, ClientContext_t *);
  client_context_send_literal(client_context,
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Encoding: gzip\r\n"
                              "Content-Length: " favicon_svg_gz_len_str "\r\n"
                              "\r\n");
  client_context_send_static(client_context, favicon_svg_gz);
  return tiny_http_parser_get_body_size(context, header);
}

static size_t route_upload(TinyHttpParserContext_t *context,
                           const TinyHttpParserHeader_t *header,
                           router_default_fn router_default) {
  ClientContext_t *const client_context =
      tiny_http_parser_get_user_context(context, ClientContext_t *);

  client_context->flash_token = get_flash_token();
  if (is_valid_flash_token(client_context->flash_token)) {
    mf_print_literal(client_context->state->context, "uploading...");
    flash_operation_queue_init(client_context->flash_token);
    return tiny_http_parser_get_body_size(context, header);
  }

  client_context_send_literal(client_context,
                              "HTTP/1.1 503 Service Unavailable\r\n"
                              "\r\n");
  client_context->custom_error = "upload unavailable";
  return SIZE_MAX;
}

static size_t route_ws(TinyHttpParserContext_t *context,
                       const TinyHttpParserHeader_t *header,
                       router_default_fn router_default) {
  ClientContext_t *const client_context =
      tiny_http_parser_get_user_context(context, ClientContext_t *);
  const TinyHttpParserRequestHeader_t *request_header = header->request_header;
  const TinyHttpParserStringView_t *ws_key = get_ws_key(request_header);
  if (ws_key != NULL && ws_key->size == WEBSOCKET_KEY_SIZE) {
    websocket_accept(ws_key->data, client_context->ws_accept_key);
    client_context_send_literal(client_context,
                                "HTTP/1.1 101 Switching Protocols\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Accept: ");
    client_context_send_static(client_context, client_context->ws_accept_key);
    client_context_send_literal(client_context, "\r\n\r\n");
    tiny_http_parser_websocket_init(
        &client_context->ws_context, client_context, on_ws_message_or_control,
        on_ws_message_or_control, on_ws_error, NULL, NULL);
    client_context->ws_upgraded = 1;
    return SIZE_MAX;
  }
  return router_default(context, header);
}

static size_t router_default(TinyHttpParserContext_t *context,
                             const TinyHttpParserHeader_t *header) {
  ClientContext_t *const client_context =
      tiny_http_parser_get_user_context(context, ClientContext_t *);

  client_context_send_literal(client_context, "HTTP/1.1 400 Bad Request\r\n"
                                              "\r\n");

  client_context->custom_error = "Unknown request";
  return SIZE_MAX;
}

size_t on_http_header(TinyHttpParserContext_t *context,
                      const TinyHttpParserHeader_t *header) {
  const TinyHttpParserRequestHeader_t *request_header = header->request_header;
  if (request_header != NULL) {
// @TODO: optimize router search
#define is_string_view_equal_to(string_view, literal)                          \
  strncmp(literal, (string_view).data, (string_view).size) == 0

    if (is_string_view_equal_to(request_header->path, "/") ||
        is_string_view_equal_to(request_header->path, "/index.html")) {
      return route_index(context, header, router_default);
    } else if (is_string_view_equal_to(request_header->path, "/favicon.svg")) {
      return route_favicon_svg(context, header, router_default);
    } else if (is_string_view_equal_to(request_header->path, "/upload")) {
      return route_upload(context, header, router_default);
    } else if (is_string_view_equal_to(request_header->path, "/ws")) {
      return route_ws(context, header, router_default);
    }
  }

  return router_default(context, header);
}

// thread local?
static ClientContext_t *sync_flag = NULL;

static void on_flash_complete(void *context) {
  ClientContext_t *const client_context = (ClientContext_t *)context;
  client_context->flash_token = 0;

  client_context_send_literal(client_context, "HTTP/1.1 200 OK\r\n"
                                              "\r\n");

  mf_print_literal(client_context->state->context, "upload completed");
  client_context->custom_error = "upload completed";
  if (sync_flag != client_context) {
    // callback is call in async
    // POST method need sever to close connection
    close_pcb(client_context->pcb, client_context->state);
  }
}

typedef struct {
  struct pbuf *p;
  ClientContext_t *client_context;
} SharedPbuf;

static SharedPbuf *new_shared_pbuf(ClientContext_t *client_context) {
  SharedPbuf *const shared_pbuf = our_new(SharedPbuf);
  assert(shared_pbuf != NULL);
  shared_pbuf->client_context = client_context;
  shared_pbuf->p = client_context->current_pbuf;
  client_context_require(shared_pbuf->client_context, shared_pbuf->p);
  return shared_pbuf;
}

static void on_shared_pbuf_dispose(void *context, const char *buffer,
                                   size_t buffer_size) {
  SharedPbuf *const shared_pbuf = (SharedPbuf *)context;
  client_context_release(shared_pbuf->client_context, shared_pbuf->p);
  our_delete(shared_pbuf);
}

size_t on_http_body_stream(TinyHttpParserContext_t *context, const char *data,
                           size_t size, size_t *remain_not_recv_size) {
  ClientContext_t *const client_context =
      tiny_http_parser_get_user_context(context, ClientContext_t *);
  if (client_context->custom_error) {
    // ignore input
    *remain_not_recv_size = SIZE_MAX;
    return size;
  }
  if (client_context->ws_upgraded) {
    return tiny_http_parser_websocket_input(&client_context->ws_context, data,
                                            size, remain_not_recv_size);
  }
  if (client_context->flash_token != 0) {
    SharedPbuf *const shared_pbuf = new_shared_pbuf(client_context);
    const DisposableBuffer_t buffer = {
        .data = data,
        .size = size,
        .context = shared_pbuf,
        .on_dispose = on_shared_pbuf_dispose,
    };
    flash_operation_queue_write(client_context->flash_token, &buffer);
    if (*remain_not_recv_size == 0) {
      const EventTask_t task = {.callback = on_flash_complete,
                                .context = client_context};
      // `flash_operation_queue_complete` may call on_complete sync
      // so it is a problem who to close pcb and release client context resource
      sync_flag = client_context;
      flash_operation_queue_complete(client_context->flash_token, &task);
      sync_flag = NULL;
    }
  }
  return size;
}

void on_http_error(TinyHttpParserContext_t *context, const char *error_reason) {
  ClientContext_t *const client_context =
      tiny_http_parser_get_user_context(context, ClientContext_t *);
  client_context->custom_error = error_reason;
}
