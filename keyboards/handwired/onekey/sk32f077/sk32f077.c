/* Copyright 2026 QMK
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "quantum.h"

/* Platform bring-up / automatic test scaffolding for the SK32F077 port.
 *
 * Everything below is compiled ONLY when the firmware is built with
 * SK32_BRINGUP_TESTS=yes (see rules.mk): automatic key-press loop on the
 * shorted PB5/PB6 pair, a USART1 'alive' heartbeat and the SK32 USB trace
 * ring events.  The production firmware is a plain 1x1 DIRECT-pin onekey on
 * PB5 and needs no custom code, so this file is empty in a production build.
 */
#if defined(SK32_BRINGUP_TESTS)

#include "ch.h"
#include "hal.h"

/* Priority used by the bring-up test threads.
 *
 * The QMK main loop runs as the ChibiOS "main" thread at NORMALPRIO and never
 * blocks (busy loop), and CH_CFG_TIME_QUANTUM is 0 (no round-robin), so a
 * thread at NORMALPRIO that is woken by a virtual timer never preempts it and
 * is starved forever.  Running the test threads strictly above NORMALPRIO lets
 * the scheduler switch to them as soon as their sleep timer expires.
 */
#ifndef SK32_TEST_PRIO
#    define SK32_TEST_PRIO ((tprio_t)(NORMALPRIO + 8U))
#endif

#if defined(SK32_USB_TRACE)
/* Emit an application event into the SK32 USB bring-up trace ring (defined
   in the SK32 USB low level driver). */
extern void sk32_usb_trace_emit(uint8_t tag, uint8_t a, uint8_t b, uint8_t c);
#endif

/* USART1 debug heartbeat.
 *
 * The board wires USART1 to PA0 (TX) and PA1 (RX) on alternate function 10.
 * A serial tool is connected to those pads, so a dedicated thread keeps
 * sending a short "alive" line every 500 ms to prove that the firmware is
 * actually running.  The RX direction is not used by this test.
 */
static const SerialConfig usart1_cfg = {
    .speed = 921600,
    .cr1   = 0, /* 8 data bits, no parity, no OVER8. */
    .cr2   = 0, /* USART_CR2_STOP1_BITS == 0: one stop bit. */
    .cr3   = 0,
};

#if defined(SK32_USB_TRACE) && defined(SK32_USB_TRACE_STREAM)
extern size_t sk32_usb_trace_drain(uint8_t *bp, size_t max);

static void uart_hex(SerialDriver *sdp, const uint8_t *buf, size_t n) {
    static const char hex[] = "0123456789ABCDEF";
    char             tmp[2];

    for (size_t i = 0U; i < n; i++) {
        tmp[0] = hex[(buf[i] >> 4U) & 0x0FU];
        tmp[1] = hex[buf[i] & 0x0FU];
        sdWriteTimeout(sdp, (const uint8_t *)tmp, 2U, TIME_MS2I(20));
    }
}
#endif /* SK32_USB_TRACE && SK32_USB_TRACE_STREAM */

static THD_WORKING_AREA(waHeartbeatThread, 256);
static THD_FUNCTION(HeartbeatThread, arg) {
    (void)arg;

    chRegSetThreadName("uart-heartbeat");
    while (true) {
#if defined(SK32_USB_TRACE)
        /* Low-rate heartbeat marker: proves this thread was created, that the
         * scheduler/system tick are alive and that sdWriteTimeout returns. */
        sk32_usb_trace_emit('H', 0U, 0U, 0U);
#endif
#if defined(SK32_USB_TRACE) && defined(SK32_USB_TRACE_STREAM)
        uint8_t buf[128];
        size_t  n = sk32_usb_trace_drain(buf, sizeof(buf));

        if (n > 0U) {
            static const char hdr[] = "\r\n[";
            sdWriteTimeout(&SD1, (const uint8_t *)hdr, sizeof(hdr) - 1U,
                           TIME_MS2I(20));
            uart_hex(&SD1, buf, n);
            static const char eol[] = "]\r\n";
            sdWriteTimeout(&SD1, (const uint8_t *)eol, sizeof(eol) - 1U,
                           TIME_MS2I(20));
        }
        chThdSleepMilliseconds(25);
#else
        /* NOTE: when the USB trace is captured through a RAM ring + OpenOCD
         * dump_image, the ring must NOT be drained here (the thread would
         * consume the events before they can be read back). Keep streaming
         * disabled unless SK32_USB_TRACE_STREAM is defined and a serial
         * adapter is actually connected to PA0/PA1. */
        static const char msg[] = "alive\r\n";
        sdWriteTimeout(&SD1, (const uint8_t *)msg, sizeof(msg) - 1U,
                       TIME_MS2I(50));
        chThdSleepMilliseconds(500);
#endif
    }
}

/* Automatic onekey key-press test.
 *
 * The keyboard is configured with a single DIRECT pin key on PB5 (pressed
 * when the pin is pulled low, internal pull-up enabled).  PB6 is wired to
 * PB5 and is configured as a push-pull output here: driving PB6 low pulls
 * PB5 low, which simulates a key press; driving it high releases the key.
 * The pin is toggled once per second, ten times, so the host should see ten
 * 'A' (KC_A) key taps over the USB HID interface.
 */
static THD_WORKING_AREA(waKeySimThread, 256);
static THD_FUNCTION(KeySimThread, arg) {
    (void)arg;

    chRegSetThreadName("key-sim");

    /* PB6 push-pull output, start released (high). */
    palSetPadMode(GPIOB, 6U, PAL_MODE_OUTPUT_PUSHPULL);
    palSetPad(GPIOB, 6U);

#if defined(SK32_USB_TRACE)
    /* 'Q' = key-sim thread entered and reached this point. */
    sk32_usb_trace_emit('Q', 0U, 0U, 0U);
#endif

    /* Let the host finish USB enumeration before pressing keys. */
    chThdSleepMilliseconds(1500);

#if defined(SK32_USB_TRACE)
    /* 'J' = the pre-test delay elapsed, press loop about to start. */
    sk32_usb_trace_emit('J', 0U, 0U, 0U);
#endif

    for (int i = 0; i < 10; i++) {
        /* Press: pull the shorted PB5/PB6 pair low for ~200 ms. */
#if defined(SK32_USB_TRACE)
        sk32_usb_trace_emit('V', (uint8_t)(i + 1U), 1U, 0U); /* press # */
#endif
        palClearPad(GPIOB, 6U);
        chThdSleepMilliseconds(200);
        /* Release for the remainder of the 1 s period. */
#if defined(SK32_USB_TRACE)
        sk32_usb_trace_emit('V', (uint8_t)(i + 1U), 0U, 0U); /* release # */
#endif
        palSetPad(GPIOB, 6U);
        chThdSleepMilliseconds(800);
    }

    /* Test done: keep PB6 high (key released) and park the thread. */
    while (true) {
        chThdSleepMilliseconds(1000);
    }
}

/* Key-chain observability hooks (automatic onekey test).
 *
 * The following hooks tag events into the same USB trace ring that is dumped
 * through OpenOCD after the run, so the different layers of the key chain can
 * be verified without a desktop/keyboard monitor:
 *   'M' matrix scan saw the row value change (a = row 0 raw value);
 *   'P' process_record_user ran (a = keycode, b = pressed flag);
 *   'N' (USB LLD) an EP IN transfer completed, i.e. a report was actually
 *       delivered to the host (a = endpoint number).
 */
static uint8_t last_matrix_row0 = 0xFFU;

void matrix_scan_user(void) {
#if defined(SK32_USB_TRACE)
    uint8_t cur = (uint8_t)matrix_get_row(0);
    if (cur != last_matrix_row0) {
        last_matrix_row0 = cur;
        sk32_usb_trace_emit('M', cur, 0U, 0U);
    }
#endif
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
#if defined(SK32_USB_TRACE)
    sk32_usb_trace_emit('P', (uint8_t)(keycode & 0xFFU),
                        (uint8_t)(record->event.pressed ? 1U : 0U),
                        (uint8_t)record->event.key.col);
#endif
    return true;
}

/* Drive the PB6/PB5 actuator line high before the matrix (direct pin on
 * PB5) is initialized, so the shorted pair reads "released" right from the
 * first scan (no spurious key at boot). */
void keyboard_pre_init_user(void) {
    palSetPadMode(GPIOB, 6U, PAL_MODE_OUTPUT_PUSHPULL);
    palSetPad(GPIOB, 6U);
}

void keyboard_post_init_user(void) {
#if defined(SK32_USB_TRACE)
    /* 'G',0 = post-init hook entered. */
    sk32_usb_trace_emit('G', 0U, 0U, 0U);
#endif

    /* Route PA0/PA1 to the USART1 alternate function 10. */
    palSetPadMode(GPIOA, 0U, PAL_MODE_ALTERNATE(10) | PAL_SK32_OSPEED_MID);
    palSetPadMode(GPIOA, 1U, PAL_MODE_ALTERNATE(10) | PAL_SK32_OSPEED_MID);

    /* Start USART1 (SD1) and spawn the heartbeat thread. */
    sdStart(&SD1, &usart1_cfg);
    chThdCreateStatic(waHeartbeatThread, sizeof(waHeartbeatThread),
                      SK32_TEST_PRIO, HeartbeatThread, NULL);

    /* Spawn the automatic key-press test thread. */
    chThdCreateStatic(waKeySimThread, sizeof(waKeySimThread),
                      SK32_TEST_PRIO, KeySimThread, NULL);

#if defined(SK32_USB_TRACE)
    /* 'G',1 = both threads created (hook left normally). */
    sk32_usb_trace_emit('G', 1U, 0U, 0U);
#endif
}

#endif /* SK32_BRINGUP_TESTS */
