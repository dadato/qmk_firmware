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
 * @file    usbdfu.h
 * @brief   stm32duino compatible USB DFU device on the SK32F077 (VID:PID
 *          1EAF:0003).  The whole protocol runs on endpoint 0.
 */

#ifndef USBDFU_H
#define USBDFU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* USB driver configuration, defined in usbdfu.c. */
extern const USBConfig usbcfg;

/* Reset request raised when a finished download leaves the manifestation
 * phase; the DFU idle loop watches it and resets into the application. */
extern volatile bool usbdfu_app_boot_pending;

/* Size of the DNLOAD receive buffer / advertised wTransferSize. */
#define FW_BUFFER_SIZE  256

/* DFU session latch: a SRAM slot at the end of the vector-table reserve
 * (0x20000000..0x200001FF) that survives warm resets.  main() reads it at
 * boot to tell a key-armed DFU session that never downloaded anything apart
 * from a real programming session, so a later reset can exit boot mode.
 * - KEYED:   the last reset entered DFU through the boot key.
 * - TOUCHED: at least one DNLOAD data block arrived during that session. */
#define SK32_DFU_LATCH_ADDR   ((volatile uint32_t *)0x200001F4UL)
#define SK32_DFU_LATCH_KEYED    0x00000001UL
#define SK32_DFU_LATCH_TOUCHED  0x00000002UL

/* DFU requests (USB class requests). */
enum dfu_req {
    DFU_DETACH = 0,
    DFU_DNLOAD = 1,
    DFU_UPLOAD = 2,
    DFU_GETSTATUS = 3,
    DFU_CLRSTATUS = 4,
    DFU_GETSTATE = 5,
    DFU_ABORT = 6,
};

/* DFU states, per the DFU 1.1 specification. */
enum dfu_state {
    STATE_APP_IDLE = 0,
    STATE_APP_DETACH,
    STATE_DFU_IDLE,
    STATE_DFU_DNLOAD_SYNC,
    STATE_DFU_DNBUSY,
    STATE_DFU_DNLOAD_IDLE,
    STATE_DFU_MANIFEST_SYNC,
    STATE_DFU_MANIFEST,
    STATE_DFU_MANIFEST_WAIT_RESET,
    STATE_DFU_UPLOAD_IDLE,
    STATE_DFU_ERROR,
};

/* DFU status codes, per the DFU 1.1 specification. */
enum dfu_status {
    DFU_STATUS_OK = 0,
    DFU_STATUS_ERR_TARGET = 1,
    DFU_STATUS_ERR_FILE = 2,
    DFU_STATUS_ERR_WRITE = 3,
    DFU_STATUS_ERR_ERASE = 4,
    DFU_STATUS_ERR_CHECK_ERASED = 5,
    DFU_STATUS_ERR_PROG = 6,
    DFU_STATUS_ERR_VERIFY = 7,
    DFU_STATUS_ERR_ADDRESS = 8,
    DFU_STATUS_ERR_NOTDONE = 9,
    DFU_STATUS_ERR_FIRMWARE = 10,
    DFU_STATUS_ERR_VENDOR = 11,
    DFU_STATUS_ERR_USBR = 12,
    DFU_STATUS_ERR_POR = 13,
    DFU_STATUS_ERR_UNKNOWN = 14,
    DFU_STATUS_ERR_STALLEDPKT = 15,
};

/* Raw layout of the 8 bytes of a SETUP packet. */
struct usb_setup {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

#ifdef __cplusplus
}
#endif

#endif /* USBDFU_H */
