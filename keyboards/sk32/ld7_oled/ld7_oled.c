#include "ld7_oled.h"

#include "hal.h"
#include "hal_low_power_lld.h"

/* Real CPU low power while the USB bus is suspended.
 * suspend_power_down_kb() is invoked on every iteration of the suspend loop
 * in protocol_pre_task() (tmk_core/protocol/chibios/chibios.c) while the bus
 * stays suspended.  We override the weak default here to enter the SK32 STOP
 * mode (deep sleep) instead of the plain wait_ms(17) busy loop: the core halts
 * until an interrupt wakes it (the forced USB RESUME kept armed by the USB
 * low level driver, or an EXTI).  On a confirmed wakeup suspend_wakeup_init_kb()
 * rebuilds the system/PLL clocks torn down by STOP.
 * Gated on SK32_HAL_USE_LOWPOWER so it only affects this board. */
#if (SK32_HAL_USE_LOWPOWER == TRUE)
void suspend_power_down_kb(void) {
    /* Enter STOP.  Returns as soon as any interrupt wakes the core; the
       suspend loop re-checks the wakeup condition and loops while the bus
       is still suspended. */
    sk32_lowpower_stop_enter();
    sk32_lowpower_stop_restore();
    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    /* STOP dropped the PLL/HSI clock config; restore it before any further
       USB/report processing.  Handled here (after the loop has determined a
       real wakeup) so an unrelated interrupt that merely wakes STOP does not
       force a full clock rebuild. */
    sk32_lowpower_stop_restore();
    suspend_wakeup_init_user();
}
#endif /* SK32_HAL_USE_LOWPOWER */

#ifdef OLED_ENABLE

#include "ch.h"
#include "oled_driver.h"

/* The whole OLED draw + blocking SSD1306 I2C render belongs to a dedicated
 * thread, not to the QMK main loop (see OLED_TASK_EXTERNAL_LOOP in config.h).
 * Main loop and thread never race on oled_buffer: the main loop's oled_task()
 * returns immediately (buffer untouched) and this thread is the only accessor,
 * serializing oled_set_cursor + oled_task_kb (redraw) then oled_render_dirty
 * (send over I2C).  Without this a drawn block could be sent half-old / half-new
 * while it is in flight on the bus, leaving a permanently "stacked" digit.
 *
 * Priority NORMALPRIO+1: it must be strictly above NORMALPRIO, otherwise, with
 * CH_CFG_TIME_QUANTUM = 0, the busy main loop never yields to it (starved).
 * It is kept only +1 (rather than the onekey bring-up +8) so that each render
 * wake preempts the main loop for at most the time to send a few blocks.
 */
static THD_WORKING_AREA(waOledRenderThread, 512);
static THD_FUNCTION(oled_render_thread, arg) {
    (void)arg;
    chRegSetThreadName("oled-render");
    while (true) {
        chThdSleepMilliseconds(OLED_UPDATE_INTERVAL);
        /* Single-owner draw+send: keeps the block consistent, no tearing. */
        oled_set_cursor(0, 0);
        oled_task_kb();
        oled_render_dirty(true);
    }
}

void keyboard_post_init_user(void) {
    /* oled_init() has already run, so oled_initialized is true here. */
    chThdCreateStatic(waOledRenderThread, sizeof(waOledRenderThread),
                      (tprio_t)(NORMALPRIO + 1U), oled_render_thread, NULL);
}

#endif /* OLED_ENABLE */

#ifdef RGB_MATRIX_ENABLE

/* The 7 LEDs sit in a single row above KEY1..KEY7.  matrix_co maps every
 * directly wired key (matrix [0][0..6]) to its LED index 0..6. */
led_config_t g_led_config = {
	{
		{0, 1, 2, 3, 4, 5, 6}
	}, {
		{0, 32}, {37, 32}, {74, 32}, {111, 32}, {148, 32}, {185, 32}, {224, 32}
	}, {
		4, 4, 4, 4, 4, 4, 4
	}
};

#endif
