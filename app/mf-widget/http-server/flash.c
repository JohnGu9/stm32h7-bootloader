#include "app.h"
#include "bootloader-common.h"
#include "http-server.h"
#include <assert.h>

// @TODO: add ignore flash error feature

static size_t _current_token = 0;
static size_t _current_offset = 0;
static int _flash_busy = 0;

size_t get_flash_token() {
  static size_t token = 1;
  if (_current_token != 0) { // limit only one token holder can operator flash
    return 0;
  }
  HAL_StatusTypeDef status = HAL_FLASH_Unlock();
  assert(status == HAL_OK);
  if (unlikely(status != HAL_OK)) {
    return 0;
  }
  if (unlikely(token == SIZE_MAX)) {
    token = 1;
  }
  _current_token = token++;
  return _current_token;
}

int is_valid_flash_token(size_t token) {
  return (token != 0 && token == _current_token) ? 1 : 0;
}

int is_flash_busy() { return _flash_busy; }

/**
 * @brief Get the flash error code when on_completed is invoked
 *
 * @return uint32_t
 */
int is_flash_has_error() {
  uint32_t error = HAL_FLASH_GetError();
  return error != HAL_FLASH_ERROR_NONE;
}

static void null_callback(void *context) {}
static EventTask_t _flash_operation_on_completed = {.callback = null_callback};

static void abort_flash_token_async(void *context);

static int is_abort() {
  return _flash_operation_on_completed.callback == abort_flash_token_async;
}

static int is_flash_available(size_t token) {
  if (!is_valid_flash_token(token) || is_flash_busy()) {
    return 0;
  }
  return 1;
}

// define where to erase app flash data
// should match `APP_START_ADDR` and `APP_SIZE` in `bootloader-common-config.h`
static FLASH_EraseInitTypeDef AppEraseInitStruct = {
    .TypeErase = FLASH_TYPEERASE_MASSERASE,
    .VoltageRange = FLASH_VOLTAGE_RANGE_3, // 2.7V to 3.6V
    .Banks = FLASH_BANK_2,
};

static int is_ok_to_flash_app_clear(size_t token) {
  if (!is_flash_available(token)) {
    return HAL_BUSY;
  }
  if (is_flash_has_error()) {
    return HAL_ERROR;
  }
  if (is_abort()) {
    return HAL_ERROR;
  }
  return HAL_OK;
}

static void *get_app_address() { return (void *)APP_START_ADDR; }

int flash_app_clear(size_t token) {
  const int ok = is_ok_to_flash_app_clear(token);
  if (unlikely(ok != HAL_OK)) {
    return ok;
  }

  uint32_t SectorError;
  HAL_StatusTypeDef status =
      HAL_FLASHEx_Erase(&AppEraseInitStruct, &SectorError);
  assert(status == HAL_OK);
  if (unlikely(status != HAL_OK)) {
    return status;
  }

  _current_offset = (size_t)get_app_address();
  return HAL_OK;
}

int flash_app_clear_async(size_t token, const EventTask_t *on_completed) {
  const int ok = is_ok_to_flash_app_clear(token);
  if (unlikely(ok != HAL_OK)) {
    return ok;
  }

  _flash_busy = 1;

  _flash_operation_on_completed = *on_completed;

  // invoke `HAL_FLASH_EndOfOperationCallback` when complete
  HAL_StatusTypeDef status = HAL_FLASHEx_Erase_IT(&AppEraseInitStruct);
  assert(status == HAL_OK);
  if (unlikely(status != HAL_OK)) {
    _flash_busy = 0;
    return status;
  }

  _current_offset = (size_t)get_app_address();
  return HAL_OK;
}

static int is_ok_to_flash_write(size_t token) {
  if (unlikely(_current_offset >= (APP_START_ADDR + APP_SIZE))) { // overflow
    return HAL_ERROR;
  }
  if (unlikely(!is_flash_available(token))) {
    return HAL_BUSY;
  }
  if (unlikely(is_flash_has_error())) {
    return HAL_ERROR;
  }
  if (unlikely(is_abort())) {
    return HAL_ERROR;
  }
  return HAL_OK;
}

int flash_write(size_t token, const uint8_t flash_word[FLASH_WORD_SIZE]) {
  const int ok = is_ok_to_flash_write(token);
  if (unlikely(ok != HAL_OK)) {
    return ok;
  }

  HAL_StatusTypeDef status = HAL_FLASH_Program(
      FLASH_TYPEPROGRAM_FLASHWORD, _current_offset, (uint32_t)flash_word);
  assert(status == HAL_OK);
  if (unlikely(status != HAL_OK)) {
    return status;
  }
  _current_offset += FLASH_WORD_SIZE;

  return HAL_OK;
}

int flash_write_async(size_t token, const uint8_t flash_word[FLASH_WORD_SIZE],
                      const EventTask_t *on_completed) {
  const int ok = is_ok_to_flash_write(token);
  if (unlikely(ok != HAL_OK)) {
    return ok;
  }

  _flash_busy = 1;

  _flash_operation_on_completed = *on_completed;
  // invoke `HAL_FLASH_EndOfOperationCallback` when complete
  HAL_StatusTypeDef status = HAL_FLASH_Program_IT(
      FLASH_TYPEPROGRAM_FLASHWORD, _current_offset, (uint32_t)flash_word);
  assert(status == HAL_OK);
  if (unlikely(status != HAL_OK)) {
    _flash_busy = 0;
    return status;
  }
  _current_offset += FLASH_WORD_SIZE;

  return HAL_OK;
}

int release_flash_token(size_t token) {
  if (!is_flash_available(token)) {
    return HAL_BUSY;
  }
  HAL_StatusTypeDef status = HAL_FLASH_Lock();
  assert(status == HAL_OK);
  _current_token = 0;
  _flash_operation_on_completed.callback = null_callback;
  return HAL_OK;
}

static void abort_flash_token_async(void *context) {
  size_t token = (size_t)context;
  assert(is_flash_available(token));
  release_flash_token(token);
}

int abort_flash_token(size_t token) {
  if (!is_valid_flash_token(token)) {
    return HAL_ERROR;
  }
  if (is_abort()) {
    return HAL_ERROR;
  }
  if (!is_flash_busy()) {
    return release_flash_token(token);
  }

  _flash_operation_on_completed.context = (void *)token;
  _flash_operation_on_completed.callback = abort_flash_token_async;
  return HAL_OK;
}

// irq

static void invoke_on_completed(void *context) {
  assert(!is_flash_has_error());
  _flash_busy = 0;
  _flash_operation_on_completed.callback(_flash_operation_on_completed.context);
}

static const EventTask_t _on_completed_task = {.callback = invoke_on_completed};

extern FLASH_ProcessTypeDef pFlash;

/**
 * @brief  FLASH end of operation interrupt callback.
 * @param  ReturnValue: The value saved in this parameter depends on the ongoing
 * procedure:
 *                      - Mass Erase: 0
 *                      - Page/Sector Erase: The page or sector that has been
 * erased
 *                      - Program: The address that was selected for data
 * programming
 * @retval None
 */
void HAL_FLASH_EndOfOperationCallback(uint32_t ReturnValue) {
  // Handle successful erase completion here
  // Example: Trigger the next page erase or start programming data
  switch (pFlash.ProcedureOnGoing) {
  case FLASH_PROC_NONE:
    // after erase all section
    schedule_task_on_main_thread(&_on_completed_task);
    break;
  case FLASH_PROC_SECTERASE_BANK1:
  case FLASH_PROC_SECTERASE_BANK2:
    // after erase each section
    // not using
    break;
  case FLASH_PROC_MASSERASE_BANK1:
  case FLASH_PROC_MASSERASE_BANK2:
  case FLASH_PROC_ALLBANK_MASSERASE:
    // after erase full bank
    schedule_task_on_main_thread(&_on_completed_task);
    break;
  case FLASH_PROC_PROGRAM_BANK1:
  case FLASH_PROC_PROGRAM_BANK2:
    // after write
    schedule_task_on_main_thread(&_on_completed_task);
    break;
  }
}

/**
 * @brief  FLASH operation error interrupt callback.
 * @param  ReturnValue: The value saved in this parameter depends on the ongoing
 * procedure.
 * @retval None
 */
void HAL_FLASH_OperationErrorCallback(uint32_t ReturnValue) {
  // Handle error scenario here
}
