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
 * @file    dfu_target.h
 * @brief   SK32F077 on-chip flash programming interface used by the DFU
 *          state machine.  All addresses are handled as absolute flash
 *          addresses.
 */

#ifndef DFU_TARGET_H
#define DFU_TARGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * SK32F077 flash geometry.
 * The "sk32duino" bootloader owns the first 16 KB; the QMK application is
 * linked at 0x08004000 and the whole remainder of the 128 KB device is
 * downloadable through DFU.
 *
 * NOTE: the page erase granularity is 2 KB (measured on silicon, see the
 * session notes), NOT the 1 KB page size of the STM32F072 register model
 * this device is otherwise compatible with.  An erase command whose address
 * register points inside a 2 KB page erases the whole page (the address is
 * effectively masked down to the page base), so page based operations must
 * always work on full, 2 KB aligned pages.
 */
#define SK32_FLASH_BASE       0x08000000UL  /**< Main flash base.             */
#define SK32_BOOT_SIZE        0x00004000UL  /**< Bootloader size (16 KB).     */
#define SK32_APP_BASE         (SK32_FLASH_BASE + SK32_BOOT_SIZE)
#define SK32_FLASH_TOTAL_SIZE 0x00020000UL  /**< Total device flash (128 KB). */
#define SK32_MAX_FW_SIZE      (SK32_FLASH_TOTAL_SIZE - SK32_BOOT_SIZE)
#define SK32_FLASH_PAGE_SIZE  0x00000800UL  /**< Erase page size (2 KB).      */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Unlocks the flash controller (write/erase enable).
 */
void target_flash_unlock(void);

/**
 * @brief   Re-locks the flash controller.
 */
void target_flash_lock(void);

/**
 * @brief   Resets the "pages already erased" bookkeeping.
 * @details Must be called once at the start of every download session
 *          (i.e. when the first chunk at offset zero is programmed).
 */
bool target_prepare_flash(void);

/**
 * @brief   Programs @p len bytes from @p src to @p dst.
 * @details The destination flash must be erased first.  Erasing is done
 *          lazily here: the 2 KB page containing each part of the buffer is
 *          erased right before it is first programmed, so only the pages
 *          actually touched by the new firmware are erased.
 * @pre     The flash controller must be unlocked.
 */
bool target_flash_write(uint8_t *dst, const uint8_t *src, size_t len);

/**
 * @brief   Maximum size of the downloadable application, in bytes.
 */
size_t target_get_max_fw_size(void);

/**
 * @brief   Poll timeout advertised to the host while a write is pending.
 */
uint16_t target_get_timeout(void);

/**
 * @brief   Called when the host closes the download (manifest phase).
 * @details Flushes the last partially filled page of the new firmware.
 * @return  @p true on success, @p false on flash programming failure.
 */
bool target_complete_programming(void);

#ifdef __cplusplus
}
#endif

#endif /* DFU_TARGET_H */
