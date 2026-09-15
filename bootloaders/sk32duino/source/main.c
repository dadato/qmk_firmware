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
 * @file    main.c
 * @brief   sk32duino bootloader entry point.
 *
 * Boot flow:
 *  1. On every reset the bootloader runs first (it owns the reset vector at
 *     0x08000000, "Boot area 16K").
 *  2. A "run application" tag left in SRAM by a previous pass makes the
 *     bootloader jump to the application (0x08004000) right here, before the
 *     HAL or the RTOS is started, so the application starts in a context
 *     identical to a hardware reset (Cortex-M0 has no VTOR, the 512 byte
 *     vector table is copied to SRAM and SRAM is remapped at address zero
 *     through SYSCFG CFGR1 MEM_MODE = 0b11).
 *  3. Otherwise the HAL/RTOS is started and DFU mode is entered when
 *       - the boot key (KB boot magic key) is held during reset, or
 *       - the application requested DFU by writing the DFU magic word at
 *         0x200001F0 (see platforms/chibios/bootloaders/sk32duino.c) and
 *         resetting.
 *  4. If DFU is not requested the bootloader stores the "run application"
 *     tag pair and performs a system reset.  Step 2 then jumps to the
 *     application from a clean reset context.
 *
 * The handshake slots live inside the SRAM area reserved for the vector
 * table copy (0x20000000 .. 0x200001FF), which the application never uses.
 */

#include <string.h>

#include "ch.h"
#include "hal.h"

#include "dfu_target.h"
#include "usbdfu.h"

/*===========================================================================*/
/* Handshake memory map.                                                     */
/*===========================================================================*/

/* Address of the 512 byte SRAM vector table copy. */
#define SK32_SRAM_VT                0x20000000UL
/* Number of 32 bit words in a 512 byte vector table. */
#define SK32_VT_WORDS               128U
/* End of the physical SRAM (0x20000000 + 10 KB). */
#define SK32_SRAM_END               0x20002800UL

/* DFU request magic, written by the application before resetting.  Its
 * value and address must match platforms/chibios/bootloaders/sk32duino.c. */
#define SK32_DFU_MAGIC              0x4B32D7A1UL
#define SK32_DFU_MAGIC_ADDR         ((volatile uint32_t *)0x200001F0UL)

/* "Run application" tag pair, set by this bootloader in step 4 above and
 * consumed at the very beginning of step 2.  A 64 bit signature makes an
 * accidental match on uninitialized SRAM practically impossible. */
#define SK32_RUN_APP_TAG0           0x4B1E2F51UL
#define SK32_RUN_APP_TAG1           0x33325453UL
#define SK32_RUN_TAG0_ADDR          ((volatile uint32_t *)0x200001E4UL)
#define SK32_RUN_TAG1_ADDR          ((volatile uint32_t *)0x200001E8UL)

/*===========================================================================*/
/* Boot key selection.                                                       */
/*===========================================================================*/

/* One of the two following macros selects which key forces DFU at reset.
 * Uncomment the one matching the keyboard the bootloader is flashed on.
 *
 * - SK32_DFU_KEY_KB17: W17PAD / "kb17" 17 key numpad, BOOTMAGIC key
 *   (NumLock = matrix[0][0], row PB2 x column PB12).
 * - SK32_DFU_KEY_ONEKEY: "sk32f077 onekey" bring-up board, its single
 *   direct key on PB5 (internal pull-up, pressed = low).
 *
 * With neither macro defined only the DFU magic word (QK_BOOT) enters DFU. */
#define SK32_DFU_KEY_KB17
/* #define SK32_DFU_KEY_ONEKEY */

/*===========================================================================*/
/* Application jump helpers.                                                 */
/*===========================================================================*/

/**
 * @brief   Copies the application vector table to SRAM, validates the image
 *          and branches to the application reset vector.
 * @return  @p true on success (never returns), @p false when the flash does
 *          not hold a plausible application image.
 * @note    Must be called before chSysInit() so that the application boots
 *          from a clean, reset-like context (Thread mode on MSP).
 */
static bool jump_to_application(void) {
    const uint32_t *src = (const uint32_t *)SK32_APP_BASE;
    uint32_t        *dst = (uint32_t *)SK32_SRAM_VT;
    uint32_t         sp, pc;
    unsigned         i;

    for (i = 0U; i < SK32_VT_WORDS; i++) {
        dst[i] = src[i];
    }
    __DSB();

    sp = dst[0];
    pc = dst[1];

    /* Sanity checks: the initial stack pointer must point into SRAM and the
     * reset vector must live inside the downloadable application flash area
     * (Thumb bit set).  The downloadable area is bounded by the runtime flash
     * size so a 128 KB-only image is refused on a 64 KB SK32.  An erased
     * (empty) flash reads back 0xFFFFFFFF. */
    if ((sp < 0x20000200UL) || (sp > SK32_SRAM_END)) {
        return false;
    }
    if ((pc < SK32_APP_BASE) ||
        (pc >= (SK32_APP_BASE + target_get_max_fw_size()))) {
        return false;
    }
    if ((pc & 1U) == 0U) {
        return false;
    }

    /* Enable the SYSCFG clock and remap SRAM at address zero: interrupt
     * vectors are then fetched from the SRAM copy even though Cortex-M0 has
     * no VTOR register. */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    __DSB();
    SYSCFG->CFGR1 = (SYSCFG->CFGR1 & ~SYSCFG_CFGR1_MEM_MODE) |
                    SYSCFG_CFGR1_MEM_MODE_0 | SYSCFG_CFGR1_MEM_MODE_1;
    __DSB();

    __disable_irq();

    /* Hand over: reload the stack pointer and branch to the application
     * reset vector.  Everything after this point belongs to the app.  The
     * two instructions are fused in one asm block because changing MSP
     * orphans the C stack frame of this function. */
    __asm volatile (
        "msr msp, %[sp]\n"
        "bx   %[pc]\n"
        :: [sp] "r" (sp), [pc] "r" (pc)
        : "memory");
    __builtin_unreachable();
}

/**
 * @brief   Returns @p true when the previous boot pass asked to run the
 *          application and consumes the request.
 * @note    Called before anything is initialized, directly on SRAM.
 */
static bool boot_app_requested(void) {
    volatile uint32_t *tag0 = SK32_RUN_TAG0_ADDR;
    volatile uint32_t *tag1 = SK32_RUN_TAG1_ADDR;

    if ((*tag0 != SK32_RUN_APP_TAG0) || (*tag1 != SK32_RUN_APP_TAG1)) {
        return false;
    }
    *tag0 = 0U;
    *tag1 = 0U;
    return true;
}

/*===========================================================================*/
/* Boot key scanning.                                                        */
/*===========================================================================*/

#if defined(SK32_DFU_KEY_KB17)
/**
 * @brief   Scans the kb17/W17PAD boot key: NumLock, matrix[0][0].
 * @details ROW2COL matrix: rows are inputs with pull-ups, columns are
 *          outputs.  The column of the key (PB12) is driven low while the
 *          other columns are driven high, then the row (PB2) is sampled.
 */
static bool boot_key_pressed(void) {
    bool pressed;

    /* All four columns as push-pull outputs. */
    palSetPadMode(GPIOB, 10U, PAL_MODE_OUTPUT_PUSHPULL);
    palSetPadMode(GPIOB, 11U, PAL_MODE_OUTPUT_PUSHPULL);
    palSetPadMode(GPIOB, 12U, PAL_MODE_OUTPUT_PUSHPULL);
    palSetPadMode(GPIOB, 13U, PAL_MODE_OUTPUT_PUSHPULL);

    /* Scan column 0 (PB12), keep the other columns released. */
    palSetPad(GPIOB, 10U);
    palSetPad(GPIOB, 11U);
    palClearPad(GPIOB, 12U);
    palSetPad(GPIOB, 13U);

    /* Row 0 = PB2, input with pull-up. */
    palSetPadMode(GPIOB, 2U, PAL_MODE_INPUT_PULLUP);
    chThdSleepMilliseconds(2);

    pressed = (palReadPad(GPIOB, 2U) == PAL_LOW);

    /* Restore the pads to inputs so the application boots on a clean pin
     * configuration (it re-initializes the pins anyway). */
    palSetPadMode(GPIOB, 2U, PAL_MODE_INPUT_PULLUP);
    palSetPadMode(GPIOB, 10U, PAL_MODE_INPUT_PULLUP);
    palSetPadMode(GPIOB, 11U, PAL_MODE_INPUT_PULLUP);
    palSetPadMode(GPIOB, 12U, PAL_MODE_INPUT_PULLUP);
    palSetPadMode(GPIOB, 13U, PAL_MODE_INPUT_PULLUP);

    return pressed;
}
#elif defined(SK32_DFU_KEY_ONEKEY)
/**
 * @brief   Scans the "sk32f077 onekey" bring-up board key: PB5 direct key
 *          with an internal pull-up, pressed = low.
 */
static bool boot_key_pressed(void) {
    bool pressed;

    palSetPadMode(GPIOB, 5U, PAL_MODE_INPUT_PULLUP);
    chThdSleepMilliseconds(2);

    pressed = (palReadPad(GPIOB, 5U) == PAL_LOW);

    return pressed;
}
#else
static bool boot_key_pressed(void) {
    return false;
}
#endif

/*===========================================================================*/
/* DFU mode.                                                                 */
/*===========================================================================*/

/**
 * @brief   Starts the DFU USB stack and idles forever.  The whole DFU
 *          protocol (including flash programming) is served by the EP0
 *          interrupt handlers, no thread is required.
 */
static void dfu_boot(void) {
#if defined(LINE_LED_GREEN)
    /* Activity LED of the generic SK32 board, harmless if not fitted. */
    palSetLineMode(LINE_LED_GREEN, PAL_MODE_OUTPUT_PUSHPULL);
    palClearLine(LINE_LED_GREEN);
#endif

    usbStart(&USBD1, &usbcfg);
    usbConnectBus(&USBD1);

    while (true) {
        /* A finished download armed this flag at the end of its
         * manifestation phase: leave the host a moment to observe the
         * final DFU status, then drop the USB bus so the host reliably
         * removes the DFU device before the application boots and
         * re-enumerates with its own descriptors (a bus-less gap well
         * above the USB 2.0 2.5us minimum). */
        if (usbdfu_app_boot_pending) {
            chThdSleepMilliseconds(750);
            usbDisconnectBus(&USBD1);
            chThdSleepMilliseconds(200);
            NVIC_SystemReset();
        }
#if defined(LINE_LED_GREEN)
        palToggleLine(LINE_LED_GREEN);
#endif
        chThdSleepMilliseconds(250);
    }
}

/*===========================================================================*/
/* Application entry point.                                                  */
/*===========================================================================*/

int main(void) {
    uint32_t dfu_magic;
    uint32_t dfu_latch;
    bool     boot_key;

    /*
     * Fast path: a previous boot pass asked to run the application.  Jump
     * before starting the HAL/RTOS so the application sees a reset-like
     * context (Cortex-M0 cannot change the stack pointer selection from
     * Thread mode, doing the jump here avoids that limitation).
     */
    if (boot_app_requested()) {
        /* jump_to_application() only returns when no valid image is found;
         * in that case offer DFU instead of bouncing between resets. */
        if (!jump_to_application()) {
            halInit();
            chSysInit();
            dfu_boot();
        }
        for (;;) {
        }
    }

    halInit();
    chSysInit();

    /* Read (and consume) the DFU request magic written by the application
     * before it reset.  SRAM is not cleared by a system reset. */
    dfu_magic = *SK32_DFU_MAGIC_ADDR;
    *SK32_DFU_MAGIC_ADDR = 0U;

    /* Session state left by the previous boot pass (also SRAM resident). */
    dfu_latch = *SK32_DFU_LATCH_ADDR;

    /* The application requested DFU explicitly (QK_BOOT): honor it. */
    if (dfu_magic == SK32_DFU_MAGIC) {
        *SK32_DFU_LATCH_ADDR = 0U; /* Start a fresh session. */
        dfu_boot();
    }

    boot_key = boot_key_pressed();

    if (boot_key) {
        if (((dfu_latch & SK32_DFU_LATCH_KEYED) != 0U) &&
            ((dfu_latch & SK32_DFU_LATCH_TOUCHED) == 0U)) {
            /* The previous key-armed DFU session never received a single
             * download block: treat this reset as an "exit DFU" request and
             * boot the application even if the key is still held, instead
             * of trapping the board in boot mode. */
            *SK32_DFU_LATCH_ADDR = 0U;
        } else {
            /* Fresh (or download-touched) key session: stay in DFU. */
            *SK32_DFU_LATCH_ADDR = SK32_DFU_LATCH_KEYED;
            dfu_boot();
        }
    }

    /* Boot the application on the next reset from a clean context. */
    *SK32_DFU_LATCH_ADDR = 0U;
    *SK32_RUN_TAG0_ADDR = SK32_RUN_APP_TAG0;
    *SK32_RUN_TAG1_ADDR = SK32_RUN_APP_TAG1;
    __DSB();

    NVIC_SystemReset();
    for (;;) {
    }
}
