/*
    sk32duino - SK32F077 USB DFU bootloader
    Copyright (C) 2026 NUTWANG/QMK

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/**
 * @file    dfu_target.c
 * @brief   SK32F077 on-chip flash backend for the DFU state machine.
 *
 * The device is register compatible with STM32F072 apart from the USB IP,
 * so the flash is programmed through the legacy CMSIS registers (FLASH->KEYR
 * unlock, page erase through CR.PER/AR/CR.STRT and 16 bit half-word program
 * through CR.PG).  Pages are erased lazily: one 1 KB page at a time, right
 * before it is first written, through a software page buffer.
 */

#include <string.h>

#include "hal.h"

#include "dfu_target.h"

#define PAGE_MASK             (SK32_FLASH_PAGE_SIZE - 1UL)

/* Software staging buffer for the currently open flash page. */
static uint8_t  page_buf[SK32_FLASH_PAGE_SIZE];
static size_t   page_base;       /* Absolute address of the staged page.     */
static bool     page_active;     /* A page is currently being staged.        */

/**
 * @brief   Waits for the flash controller to become idle.
 * @return  @p false if the operation did not finish within the watchdog
 *          bound, @p true otherwise.
 */
static bool flash_wait_idle(void) {
  size_t guard = 2000000U;

  while ((FLASH->SR & FLASH_SR_BSY) != 0U) {
    if (--guard == 0U) {
      return false;
    }
  }

  return true;
}

/**
 * @brief   Clears and checks the flash error flags.
 * @return  @p false if a write-protection or programming error is pending.
 */
static bool flash_clear_and_check_errors(void) {
  uint32_t sr = FLASH->SR;

  /* The error flags are cleared by writing ones. */
  FLASH->SR = 0x0000FFFFU;

  return (sr & (FLASH_SR_WRPERR | FLASH_SR_PGERR)) == 0U;
}

/**
 * @brief   Waits out the tail of an on-going page erase.
 * @details Empirically the SK32 flash controller does not always complete
 *          the page erase before the BSY flag clears: a 2048 byte
 *          program launched right after STRT reliably loses its first
 *          half (observed: only the second 1 KB of every page survived
 *          while the first 1 KB was wiped by the still-running erase).
 *          Spin past the worst-case page erase time on top of the BSY
 *          poll so that no program word races the erase.
 * @note    Executed in interrupt context (GETSTATUS processing); a plain
 *          CPU loop is used instead of a thread sleep.
 */
static void flash_erase_pause(void) {
  volatile uint32_t guard = 400000U; /* ~20-25 ms @ 48 MHz. */

  while (guard-- != 0U) {
    __asm volatile ("nop");
  }
}

/**
 * @brief   Erases the 2 KB flash page containing @p page_addr.
 * @note    @p page_addr must point to the start of a flash page.
 */
static bool flash_erase_page(uint32_t page_addr) {
  bool ok;

  if (!flash_wait_idle()) {
    return false;
  }
  FLASH->SR = 0x0000FFFFU;

  /* Page erase sequence: PER -> AR -> STRT. */
  FLASH->CR |= FLASH_CR_PER;
  FLASH->AR  = page_addr;
  FLASH->CR |= FLASH_CR_STRT;

  ok = flash_wait_idle();
  flash_erase_pause();
  FLASH->CR &= ~FLASH_CR_PER;

  return ok ? flash_clear_and_check_errors() : false;
}

/**
 * @brief   Programs one page buffer with 16 bit half-word writes.
 * @note    Half words which are still fully erased (0xFFFF) are skipped.
 */
static bool flash_program_page(const uint8_t *data, size_t len) {
  volatile uint16_t *dst = (volatile uint16_t *)page_base;
  size_t              hw_count;

  if (!flash_wait_idle()) {
    return false;
  }
  FLASH->SR = 0x0000FFFFU;
  FLASH->CR |= FLASH_CR_PG;

  hw_count = (len + 1U) / 2U;
  while (hw_count > 0U) {
    uint16_t hw = (uint16_t)data[0] | ((uint16_t)data[1] << 8U);

    if (hw != 0xFFFFU) {
      *dst = hw;
      if (!flash_wait_idle() || !flash_clear_and_check_errors()) {
        FLASH->CR &= ~FLASH_CR_PG;
        return false;
      }
    }
    dst += 1U;
    data += 2U;
    hw_count -= 1U;
  }

  FLASH->CR &= ~FLASH_CR_PG;

  return true;
}

/**
 * @brief   Erases the staged page and programs its buffer content.
 */
static bool flash_flush_page(void) {
  bool ok;

  if (!page_active) {
    return true;
  }

  ok = flash_erase_page((uint32_t)page_base);
  if (ok) {
    ok = flash_program_page(page_buf, sizeof(page_buf));
  }

  page_active = false;
  page_base = 0U;

  return ok;
}

/**
 * @brief   Unlocks the flash controller for the programming session.
 */
void target_flash_unlock(void) {
  FLASH->KEYR = FLASH_FKEY1;
  FLASH->KEYR = FLASH_FKEY2;
}

/**
 * @brief   Re-locks the flash controller.
 */
void target_flash_lock(void) {
  FLASH->CR |= FLASH_CR_LOCK;
}

/**
 * @brief   Resets the lazy-erase bookkeeping for a new download session.
 */
bool target_prepare_flash(void) {
  page_active = false;
  page_base   = 0U;

  return true;
}

/**
 * @brief   Buffers a chunk of the new firmware into the open flash page.
 */
bool target_flash_write(uint8_t *dstp, const uint8_t *src, size_t len) {
  size_t dst = (size_t)dstp;

  /* Refuse to touch the bootloader area or to go past the end of flash. */
  if ((dst < SK32_APP_BASE) ||
      (dst + len > SK32_APP_BASE + target_get_max_fw_size())) {
    return false;
  }

  while (len > 0U) {
    size_t page   = dst & ~PAGE_MASK;
    size_t offset = dst - page;
    size_t n      = SK32_FLASH_PAGE_SIZE - offset;

    if (n > len) {
      n = len;
    }

    if (page_active && (page != page_base)) {
      if (!flash_flush_page()) {
        return false;
      }
    }
    if (!page_active) {
      /* New page, initialize the buffer to the erased state. */
      memset(page_buf, 0xFF, sizeof(page_buf));
      page_base   = page;
      page_active = true;
    }

    memcpy(&page_buf[offset], src, n);
    src += n;
    dst += n;
    len -= n;

    /* The buffer is full, erase + program the page now. */
    if (offset + n == SK32_FLASH_PAGE_SIZE) {
      if (!flash_flush_page()) {
        return false;
      }
    }
  }

  return true;
}

/**
 * @brief   Returns the total on-chip flash size of the current device.
 * @details Read at runtime from the F_SIZE register (value in KB) and scaled
 *          to bytes.  The register is a fixed, factory-programmed value and
 *          the read is a plain volatile load with no HAL or clock dependency,
 *          so it is safe to call before halInit() (e.g. in the application
 *          jump validation).  No static cache is used on purpose: SRAM is not
 *          cleared by a system reset, so caching would risk a stale value.
 */
size_t target_get_flash_total_size(void) {
  uint32_t kb = (*SK32_FLASH_SIZE_REG) & 0xFFFFU;
  return (size_t)kb << 10U;
}

/**
 * @brief   Returns the maximum downloadable firmware size.
 */
size_t target_get_max_fw_size(void) {
  return target_get_flash_total_size() - SK32_BOOT_SIZE;
}

/**
 * @brief   Returns the poll timeout advertised to the host.
 * @note    All programming is performed synchronously before the status
 *          response is sent, so no extra host side delay is required.
 */
uint16_t target_get_timeout(void) {
  return 0;
}

/**
 * @brief   Flushes the last partially filled page (manifest phase).
 * @return  @p true on success, @p false on flash programming failure.
 */
bool target_complete_programming(void) {
  return flash_flush_page();
}
