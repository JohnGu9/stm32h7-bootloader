#include "app.h"
#include "mf.h"
#include "widgets.h"

static void null_print(void *, const DisposableBuffer_t *buffer) {
  dispose_buffer(buffer);
}

static void on_link_state(void *context) {
  MainWidgetState_t *const state = (MainWidgetState_t *)context;
  m_set_state(state->context);
}

static void *init_state(mContext_t *context) {
  MainWidgetState_t *const state = our_new(MainWidgetState_t);
  state->context = context;
  state->on_on_link_state_task = (EventTask_t){
      .callback = on_link_state,
      .context = state,
  };
  state->my_print = (Print_t){
      .print = null_print,
  };
  add_task(&link_state_listeners, &state->on_on_link_state_task);
  return state;
}

static void build(mContext_t *context, mWidget_t children[MF_MAX_CHILDREN]) {
  if (is_link_state_up()) {
    children[0] = (mWidget_t){.class = &HttpServerWidgetClass};
  } else {
    children[0] = NullWidget;
  }
  return;
}

static void dispose(mContext_t *context) {
  m_get_state_cast(state, context, MainWidgetState_t);
  remove_task(&link_state_listeners, &state->on_on_link_state_task);
  our_delete(state);
  return;
};

const mWidgetClass_t MainWidgetClass = {
    .init_state = &init_state,
    .build = &build,
    .should_rebuild = m_always_rebuild,
    .dispose = &dispose,
};

static void invoke_print(const Print_t *my_print,
                         const DisposableBuffer_t *buffer) {
  my_print->print(my_print->context, buffer);
}

void mf_print(mContext_t *context, const DisposableBuffer_t *buffer) {
  m_get_state_from_inherited_widget_class_cast(
      main_widget_state, context, &MainWidgetClass, MainWidgetState_t);
  if (main_widget_state != NULL) {
    invoke_print(&main_widget_state->my_print, buffer);
  }
}

void set_mf_print(mContext_t *context, const Print_t *my_print) {
  m_get_state_from_inherited_widget_class_cast(
      main_widget_state, context, &MainWidgetClass, MainWidgetState_t);
  if (main_widget_state != NULL) {
    main_widget_state->my_print = *my_print;
  }
}

void reset_mf_print(mContext_t *context) {
  m_get_state_from_inherited_widget_class_cast(
      main_widget_state, context, &MainWidgetClass, MainWidgetState_t);
  if (main_widget_state != NULL) {
    main_widget_state->my_print = (Print_t){
        .print = null_print,
    };
  }
}
