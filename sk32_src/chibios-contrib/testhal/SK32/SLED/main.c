/*
    ChibiOS - Copyright (C) 2006-2026 Giovanni Di Sirio.
    Copyright (C) 2026 QMK

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

#include "ch.h"
#include "hal.h"
#include "hal_sled_lld.h"

/*===========================================================================*/
/* SLED1 frame test on the GENERIC_SK32_F077 board.                          */
/*===========================================================================*/

/*
 * Test resources used by this test:
 *
 *  - PC0 (SLED1_CH0 group output), alternate function 14.  A WS2812/SK6812
 *    string (at least one LED) must be wired to the pad: the SLED block
 *    serializes every data byte into one channel byte, so the three byte
 *    frame below programs a single LED (wire order GRB, no bit packing).
 *  - PB14 / PB15: two board LEDs wired to push-pull outputs, the PAL
 *    abstraction drives them high (PAL_HIGH) to switch the LEDs on.
 *
 * Test procedure:
 *  - The SLED1 output pad is routed to alternate function 14 and the SLED
 *    subsystem is started with the vendor RGBKeyboardSTK time codes
 *    (T0H=4, T1H=15, TRST=80 cycles, clock /4, baud /32).
 *  - A one LED frame is sent every 300ms through @p sled_lld_send_bytes()
 *    cycling through pure green, pure red and pure blue; a successful
 *    synchronous frame transfer is acknowledged when the function returns
 *    @p MSG_OK, which also means the whole frame including the reset pulse
 *    has been shifted out.
 *  - PB15 is toggled after every successful frame (visible blinking) while
 *    PB14 provides a 1 Hz heartbeat.
 *  - On the first failure (frame not acknowledged in time) both LEDs are
 *    switched on solid and the system halts.
 */

/* Blinker thread, times are in milliseconds. */
static THD_WORKING_AREA(waBlinkThread, 128);
static THD_FUNCTION(BlinkThread, arg) {
  (void)arg;
  chRegSetThreadName("led-blink");
  while (true) {
    palSetPad(GPIOB, GPIOB_PIN14);
    chThdSleepMilliseconds(500);

    palClearPad(GPIOB, GPIOB_PIN14);
    chThdSleepMilliseconds(500);
  }
}

/*
 * Application entry point.
 */
int main(void) {

  /* One WS2812 color frame (3 bytes, GRB wire order) programmed with the
     vendor time codes used by the 3Think RGBKeyboardSTK reference. */
  static const SLEDConfig sled1_cfg = {
    .t0h_cycles      = 4U,
    .t1h_cycles      = 15U,
    .trst_cycles     = 80U,
    .prescaler       = 3U,
    .baud_prescaler  = 31U,
    .idle_polarity   = SLED_POLARITY_LOW,
    .reset_polarity  = SLED_POLARITY_LOW
  };

  /* The stream order is GRB: pure green, red and blue. */
  static const uint8_t colors[3][3] = {
    {0xFFU, 0x00U, 0x00U},        /* Green.                                  */
    {0x00U, 0xFFU, 0x00U},        /* Red.                                    */
    {0x00U, 0x00U, 0xFFU}         /* Blue.                                   */
  };
  uint8_t frame[3];
  unsigned phase = 0U;
  bool     led   = false;

  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  halInit();
  chSysInit();

  /* Configuring the PB14/PB15 LED lines as push-pull outputs, initially off. */
  palSetPadMode(GPIOB, GPIOB_PIN14, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOB, GPIOB_PIN15, PAL_MODE_OUTPUT_PUSHPULL);
  palClearPad(GPIOB, GPIOB_PIN14);
  palClearPad(GPIOB, GPIOB_PIN15);

  /*
   * Creating the LED blinker thread.
   */
  chThdCreateStatic(waBlinkThread, sizeof(waBlinkThread), NORMALPRIO,
                    BlinkThread, NULL);

  /*
   * SLED1 pads setup on alternate function 14:
   *  - PC0 = SLED1_CH0 data output (drives the external WS2812 string).
   */
  palSetPadMode(GPIOC, 0U, PAL_MODE_ALTERNATE(14) |
                           PAL_SK32_OSPEED_HIGHEST);

  /*
   * Starting the SLED subsystem: the native low level driver programs the
   * TCR/CDR time codes, allocates the DMA1 channel 1, routes it to the SLED1
   * request line through the SYSCFG CFGR3 remapping and enables the block.
   */
  sled_lld_init();
  sled_lld_start(&sled1_cfg);

  /*
   * Running the frame test forever.
   */
  while (true) {
    /* Programming the color of the single LED and sending the frame.  The
       call is synchronous and returns MSG_OK only when the whole frame (data
       plus the trailing reset pulse) has been shifted out.*/
    frame[0] = colors[phase][0];
    frame[1] = colors[phase][1];
    frame[2] = colors[phase][2];
    if (sled_lld_send_bytes(SLED1, frame, sizeof(frame)) != MSG_OK) {
      palSetPad(GPIOB, GPIOB_PIN14);
      palSetPad(GPIOB, GPIOB_PIN15);
      chSysHalt("SLED1 frame FAIL");
    }

    /* Signaling a successful frame with a PB15 toggle.*/
    if (led) {
      palClearPad(GPIOB, GPIOB_PIN15);
      led = false;
    }
    else {
      palSetPad(GPIOB, GPIOB_PIN15);
      led = true;
    }

    phase = (phase + 1U) % 3U;
    chThdSleepMilliseconds(300);
  }
}
