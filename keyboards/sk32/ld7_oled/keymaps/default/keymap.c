#include QMK_KEYBOARD_H

#include "ch.h"      // chVTGetSystemTimeX(), CH_CFG_ST_FREQUENCY
#include "matrix.h"  // matrix_scan_user()
#include <stdio.h>   // snprintf()

/* matrix_common.c provides this raw/debounced matrix reader as a weak helper
 * but does not expose it through a public header (dip_switch.c declares it the
 * same way).  peek_matrix(row, col, true) reads the pre-debounce state. */
extern bool peek_matrix(uint8_t row_index, uint8_t col_index, bool raw);

/* LD7_OLED default keymap.
 *
 * KEY1..KEY7 = Z X C V A Ctrl Enter
 */
enum layers { _BASE };

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_BASE] = LAYOUT_ld7(KC_Z, KC_X, KC_C, KC_V, KC_A, KC_LCTL, KC_ENT),
};

/* -------------------------------------------------------------------------
 *  Key statistics shown on the 128x32 OLED
 *
 *  Left  : average number of key triggers per second over the last 10 s
 *  Right : latency (ms, one decimal) from the latest key trigger to the HID
 *          report being sent
 * ---------------------------------------------------------------------- */

#define KEY_COUNT           MATRIX_COLS /* 7 directly wired keys */
#define RATE_WINDOW_SECONDS 10

/* The latency is reported with 0.1 ms resolution, so all timing maths is done
 * in "deci-milliseconds".  CH_CFG_ST_FREQUENCY is the ChibiOS system tick
 * frequency (100 kHz on this board), giving a 10 us time base. */
#define ST_TICKS_PER_MS (CH_CFG_ST_FREQUENCY / 1000u)
#define TICKS_TO_DMS(t) ((uint32_t)(t) * 10u / ST_TICKS_PER_MS)

/* Press-rate ring buffer: one bucket per second covering the last 10 s. */
static uint16_t rate_buckets[RATE_WINDOW_SECONDS];
static uint8_t  rate_index;
static uint16_t presses_this_second;
static uint32_t second_anchor;

/* Raw (pre-debounce) press timestamps used as the latency start point. */
static systime_t raw_press_ticks[KEY_COUNT];
static bool      raw_pressed[KEY_COUNT];

/* Latency from the latest key trigger to the report being sent, in 0.1 ms. */
static volatile uint32_t last_latency_dms;
static systime_t         pending_press_ticks;
static bool              pending_latency;

void matrix_scan_user(void) {
    systime_t now = chVTGetSystemTimeX();

    /* 1) Timestamp the raw press edge (independent of debouncing) of each key
     *    so the reported latency covers the debounce delay as well. */
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        bool pressed = peek_matrix(0, i, true);
        if (pressed && !raw_pressed[i]) {
            raw_press_ticks[i] = now;
        }
        raw_pressed[i] = pressed;
    }

    /* 2) Roll the presses-per-second ring buffer once every second. */
    uint32_t now_ms = timer_read32();
    if (now_ms - second_anchor >= 1000u) {
        rate_buckets[rate_index] = presses_this_second;
        presses_this_second       = 0;
        rate_index                = (rate_index + 1) % RATE_WINDOW_SECONDS;
        second_anchor += 1000u;
        if (now_ms - second_anchor >= 1000u) {
            second_anchor = now_ms; // catch up if the loop was blocked
        }
    }
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        presses_this_second++;

        uint8_t idx = record->event.key.col;
        if (idx < KEY_COUNT) {
            pending_press_ticks = raw_press_ticks[idx];
            pending_latency     = true;
        }
    }
    return true;
}

void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
    /* Called right after the key action has been performed and the HID report
     * queued to the host. */
    if (record->event.pressed && pending_latency) {
        last_latency_dms = TICKS_TO_DMS(chVTGetSystemTimeX() - pending_press_ticks);
        pending_latency  = false;
    }
}

/* -------------------------------- OLED ---------------------------------- */

static void format_fixed1(char *buf, size_t size, uint32_t value_d10) {
    snprintf(buf, size, "%lu.%lu", (unsigned long)(value_d10 / 10u), (unsigned long)(value_d10 % 10u));
}

static void oled_write_padded(const char *s, uint8_t width) {
    uint8_t n = 0;
    for (; s[n] != '\0' && n < width; n++) {
        oled_write_char(s[n], false);
    }
    for (; n < width; n++) {
        oled_write_char(' ', false);
    }
}

bool oled_task_user(void) {
    static uint32_t shown_rate = 0xFFFFFFFFu;
    static uint32_t shown_lat  = 0xFFFFFFFFu;

    uint32_t total = 0;
    for (uint8_t i = 0; i < RATE_WINDOW_SECONDS; i++) {
        total += rate_buckets[i];
    }
    uint32_t rate_d10 = (total * 10u) / RATE_WINDOW_SECONDS; // average per second
    uint32_t lat_dms  = last_latency_dms;

    if (rate_d10 != shown_rate || lat_dms != shown_lat) {
        shown_rate = rate_d10;
        shown_lat  = lat_dms;

        char buf[12];

        oled_set_cursor(0, 0);
        oled_write("KEY/s", false);
        oled_set_cursor(13, 0);
        oled_write("LAT/ms", false);

        oled_set_cursor(0, 2);
        format_fixed1(buf, sizeof(buf), rate_d10);
        oled_write_padded(buf, 6);

        oled_set_cursor(13, 2);
        format_fixed1(buf, sizeof(buf), lat_dms);
        oled_write_padded(buf, 6);
    }
    return false;
}
