#ifndef WIDGET_H
#define WIDGET_H

#include "app.h"

#include "mf.h"

typedef void (*print_fn)(void *context, const DisposableBuffer_t *buffer);

typedef struct {
  print_fn print;
  void *context;
} Print_t;

typedef struct {
  Print_t my_print;
  EventTask_t on_on_link_state_task;
  mContext_t *context;
} MainWidgetState_t;

extern const mWidgetClass_t MainWidgetClass;

extern const mWidgetClass_t HttpServerWidgetClass;

void mf_print(mContext_t *context, const DisposableBuffer_t *buffer);
#define mf_print_literal(context, literal)                                     \
  do {                                                                         \
    const DisposableBuffer_t buffer = LiteralDisposableBuffer(literal);        \
    mf_print(context, &buffer);                                                \
  } while (0)

void set_mf_print(mContext_t *context, const Print_t *my_print);

void reset_mf_print(mContext_t *context);

#endif
