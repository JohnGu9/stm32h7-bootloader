#include "app.h"
#include "http-server.h"
#include "tiny-http-parser.h"

const TinyHttpParserStringView_t *
get_ws_key(const TinyHttpParserRequestHeader_t *request_header) {
  if (request_header->headers_list != NULL) {
    const TinyHttpParserPair_t *headers_list = request_header->headers_list;
    for (size_t i = 0; i < request_header->headers_list_size; i++) {
      const TinyHttpParserPair_t *pair = &headers_list[i];
#define FMT_LITERAL(literal) (literal), (sizeof(literal) - 1)
      if (tiny_http_parser_string_view_compare(
              &pair->key, FMT_LITERAL("Sec-WebSocket-Key")) == 0) {
        return &pair->value;
      }
    }
  }
  return NULL;
}

void on_ws_message_or_control(TinyHttpParserWebsocketContext_t *context,
                              uint8_t opcode, const char *data, size_t size) {
  ClientContext_t *const client_context =
      tiny_http_parser_get_user_context(context, ClientContext_t *);
  switch (opcode) {
  case 1: // UTF-8 text
  case 2: // Binary
  {
    static const uint8_t echo_message[] = {
        (uint8_t)0x81, (uint8_t)10, 'b', 'o', 'o', 't',
        'l',           'o',         'a', 'd', 'e', 'r'};
    client_context_send_static(client_context, echo_message);
    if (size == 4 && memcmp("exit", data, size) == 0) {
      exit_main_thread();
    }
    break;
  }
  case 8: // close event
  {
    static const uint8_t close_message[] = {0x88, 0x00};
    client_context_send_static(client_context, close_message);
    tiny_http_parser_websocket_deinit(context);
    client_context->ws_upgraded = 0;
    break;
  }
  case 9: // ping event
  {
    static const uint8_t pong_message[] = {0x8a, 0x00};
    client_context_send_static(client_context, pong_message);
    break;
  }
  case 10: // pong event
  default:
    break;
  }
}

void on_ws_error(TinyHttpParserWebsocketContext_t *context,
                 const char *reason) {
  ClientContext_t *const client_context =
      tiny_http_parser_get_user_context(context, ClientContext_t *);
  client_context->custom_error = reason;
}
