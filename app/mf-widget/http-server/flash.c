#include "app.h"
#include "bootloader-common.h"
#include "http-server.h"
#include <assert.h>
#include <stdint.h>

static size_t current_token = 0;
static size_t current_offset = 0;
static int flash_busy = 0;

void *get_app_address() { return (void *)APP_START_ADDR; }

size_t get_flash_token() {
  static size_t token = 1;
  if (current_token != 0) { // limit only one token holder can operator flash
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
  current_token = token++;
  return current_token;
}

int is_valid_flash_token(size_t token) {
  return (token != 0 && token == current_token) ? 1 : 0;
}

int is_flash_busy() { return flash_busy; }

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
static EventTask_t flash_operation_on_completed = {.callback = null_callback};

extern FLASH_ProcessTypeDef pFlash;

static void invoke_on_completed(void *context) {
  assert(!is_flash_has_error());
  flash_busy = 0;
  flash_operation_on_completed.callback(flash_operation_on_completed.context);
}

static void abort_flash_token_async(void *context);

static int is_abort() {
  return flash_operation_on_completed.callback == abort_flash_token_async;
}

static int is_flash_available(size_t token) {
  if (!is_valid_flash_token(token) || is_flash_busy()) {
    return 0;
  }
  return 1;
}

static FLASH_EraseInitTypeDef EraseInitStruct = {
    .TypeErase = FLASH_TYPEERASE_SECTORS,
    .VoltageRange = FLASH_VOLTAGE_RANGE_3,
    .Banks = FLASH_BANK_2,
    .Sector = FLASH_SECTOR_0,
    .NbSectors = FLASH_SECTOR_7 + 1 - FLASH_SECTOR_0,
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

int flash_app_clear(size_t token) {
  const int ok = is_ok_to_flash_app_clear(token);
  if (unlikely(ok != HAL_OK)) {
    return ok;
  }

  uint32_t SectorError;
  HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError);
  assert(status == HAL_OK);
  if (unlikely(status != HAL_OK)) {
    return status;
  }

  current_offset = (size_t)get_app_address();
  return HAL_OK;
}

int flash_app_clear_async(size_t token, const EventTask_t *on_completed) {
  const int ok = is_ok_to_flash_app_clear(token);
  if (unlikely(ok != HAL_OK)) {
    return ok;
  }

  flash_busy = 1;

  flash_operation_on_completed = *on_completed;

  // invoke `HAL_FLASH_EndOfOperationCallback` when complete
  HAL_StatusTypeDef status = HAL_FLASHEx_Erase_IT(&EraseInitStruct);
  assert(status == HAL_OK);
  if (unlikely(status != HAL_OK)) {
    flash_busy = 0;
    return status;
  }

  current_offset = (size_t)get_app_address();
  return HAL_OK;
}

static int is_ok_to_flash_app_write(size_t token) {
  if (unlikely(current_offset >= (APP_START_ADDR + APP_SIZE))) { // overflow
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

int flash_app_write(size_t token, const uint8_t flash_word[FLASH_WORD_SIZE]) {
  const int ok = is_ok_to_flash_app_write(token);
  if (unlikely(ok != HAL_OK)) {
    return ok;
  }

  HAL_StatusTypeDef status = HAL_FLASH_Program(
      FLASH_TYPEPROGRAM_FLASHWORD, current_offset, (uint32_t)flash_word);
  assert(status == HAL_OK);
  if (unlikely(status != HAL_OK)) {
    return status;
  }
  current_offset += FLASH_WORD_SIZE;

  return HAL_OK;
}

int flash_app_write_async(size_t token,
                          const uint8_t flash_word[FLASH_WORD_SIZE],
                          const EventTask_t *on_completed) {
  const int ok = is_ok_to_flash_app_write(token);
  if (unlikely(ok != HAL_OK)) {
    return ok;
  }

  flash_busy = 1;

  flash_operation_on_completed = *on_completed;
  // invoke `HAL_FLASH_EndOfOperationCallback` when complete
  HAL_StatusTypeDef status = HAL_FLASH_Program_IT(
      FLASH_TYPEPROGRAM_FLASHWORD, current_offset, (uint32_t)flash_word);
  assert(status == HAL_OK);
  if (unlikely(status != HAL_OK)) {
    flash_busy = 0;
    return status;
  }
  current_offset += FLASH_WORD_SIZE;

  return HAL_OK;
}

int release_flash_token(size_t token) {
  if (!is_flash_available(token)) {
    return HAL_BUSY;
  }
  HAL_StatusTypeDef status = HAL_FLASH_Lock();
  assert(status == HAL_OK);
  current_token = 0;
  flash_operation_on_completed.callback = null_callback;
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

  flash_operation_on_completed.context = (void *)token;
  flash_operation_on_completed.callback = abort_flash_token_async;
  return HAL_OK;
}

static const EventTask_t on_completed_task = {.callback = invoke_on_completed};

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
    // after erase
    schedule_task_on_main_thread(&on_completed_task);
    break;
  case FLASH_PROC_SECTERASE_BANK1:
  case FLASH_PROC_SECTERASE_BANK2:
    // not using
    break;
  case FLASH_PROC_MASSERASE_BANK1:
  case FLASH_PROC_MASSERASE_BANK2:
  case FLASH_PROC_ALLBANK_MASSERASE:
    // not using
    break;
  case FLASH_PROC_PROGRAM_BANK1:
  case FLASH_PROC_PROGRAM_BANK2:
    // after write
    schedule_task_on_main_thread(&on_completed_task);
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
