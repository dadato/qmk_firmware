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
 * SK32 flash geometry.
 * The "sk32duino" bootloader owns the first 16 KB; the QMK application is
 * linked at 0x08004000.  The total device flash size is read at runtime from
 * the F_SIZE register (value in KB), so the SAME boot image adapts to 64 KB
 * and 128 KB SK32 variants without a separate build per capacity.
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
/* F_SIZE register: total internal flash size, in KB.  Value is read at runtime
   so one boot image serves every SK32 capacity.
   NOTE: measured on silicon the KByte value occupies the LOW byte only, the
   upper bits of the 32-bit read are not driven and come back as ones (a 128 KB
   part reads 0xFF80 here), so the value must be masked with
   SK32_FLASH_SIZE_MASK -- masking with 0xFFFF yields a bogus 65408 KB. */
#if !defined(SK32_FLASH_SIZE_REG)
#define SK32_FLASH_SIZE_REG   ((volatile uint32_t *)0x1FFFF7CCUL)
#endif
#if !defined(SK32_FLASH_SIZE_MASK)
#define SK32_FLASH_SIZE_MASK  0x000000FFUL
#endif
#define SK32_FLASH_PAGE_SIZE  0x00000800UL  /**< Erase page size (2 KB).      */

/*
 * Reserved-SRAM handshake slots recording the CRC32 of the last DFU
 * download so a later boot pass can verify the application image before
 * jumping to it.  The 0x20000000 .. 0x200001FF vector-table copy area is
 * never used by the application (whose data/BSS start at 0x20000200) and is
 * NOT cleared by a system reset, so the record survives from the download
 * pass into the next boot.  Slots are placed to avoid the DFU magic word at
 * 0x200001F0 and the run-application tags at 0x200001E4/E8.
 */
#define SK32_APP_CRC_MAGIC_ADDR ((volatile uint32_t *)0x200001D0UL)
#define SK32_APP_CRC_ADDR       ((volatile uint32_t *)0x200001D4UL)
#define SK32_APP_CRC_SIZE_ADDR  ((volatile uint32_t *)0x200001D8UL)
#define SK32_APP_CRC_MAGIC      0x4B524331UL  /**< "CRC1" valid record tag.   */

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
 * @brief   Total on-chip flash size of the current device, in bytes.
 * @details Read at runtime from the F_SIZE register, so a single boot image
 *          serves every SK32 capacity.  A pure volatile read with no HAL or
 *          clock dependency, safe to call before halInit().
 */
size_t target_get_flash_total_size(void);

/**
 * @brief   Maximum size of the downloadable application, in bytes.
 * @details Derived at runtime: total flash size minus the bootloader area.
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

/**
 * @brief   Records the CRC32 of the just-downloaded application.
 * @details Called at the end of the manifest phase after the last flash page
 *          is flushed.  Computes a CRC32 over the on-chip bytes the download
 *          actually wrote and stores it (with the size) in reserved SRAM so
 *          the next boot pass can verify the image before jumping.
 */
void target_store_app_crc32(void);

/**
 * @brief   Verifies the application image integrity against the record left
 *          by the last DFU download.
 * @return  @p true  when no record exists (fall back to the vector-table
 *          plausibility check) or the CRC32 of the on-chip app matches;
 *          @p false when a record exists but the app on flash does NOT match
 *          it (the image is corrupted -> the bootloader must stay in DFU).
 */
bool target_app_crc32_valid(void);

#ifdef __cplusplus
}
#endif

#endif /* DFU_TARGET_H */
