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

/*===========================================================================*/
/* GPIO test use case on the GENERIC_SK32_F077 board.                        */
/*===========================================================================*/

/*
 * Board resources used by this test:
 *
 *  - LINE_LED_GREEN (PA5): on-board LED wired to a push-pull output, the
 *    PAL abstraction drives it high (PAL_HIGH) to switch the LED on.
 *  - LINE_BUTTON (PC13): on-board button, the pin is reconfigured below as
 *    an input with the internal pull-up enabled.  The button shorts the pin
 *    to ground when pressed, so palReadLine() returns PAL_LOW while pressed
 *    and PAL_HIGH when released.
 *
 * Both the line based APIs (palSetLineMode(), palWriteLine(), ...) and the
 * equivalent pad based APIs (palSetPadMode(), palSetPad(), ...) are
 * exercised because on this port they operate on the same underlying GPIO
 * register block.
 */

/* Blinker thread, times are in milliseconds.  The LED blink sequence is
   generated using the different output write APIs in turn. */
static THD_WORKING_AREA(waBlinkThread, 128);
static THD_FUNCTION(BlinkThread, arg) {
  (void)arg;
  chRegSetThreadName("gpio-blink");
  while (true) {
    /* palWriteLine(). */
    palWriteLine(LINE_LED_GREEN, PAL_LOW);
    chThdSleepMilliseconds(250);
    palWriteLine(LINE_LED_GREEN, PAL_HIGH);
    chThdSleepMilliseconds(250);
    /* palToggleLine(). */
    palToggleLine(LINE_LED_GREEN);
    chThdSleepMilliseconds(250);
    palToggleLine(LINE_LED_GREEN);
    chThdSleepMilliseconds(250);
  }
}

/* Button thread: reads the button line and turns the LED on while the
   button is held down, overriding the blinker. */
static THD_WORKING_AREA(waButtonThread, 128);
static THD_FUNCTION(ButtonThread, arg) {
  (void)arg;
  chRegSetThreadName("gpio-button");
  while (true) {
    if (palReadLine(LINE_BUTTON) == PAL_LOW) {
      palWriteLine(LINE_LED_GREEN, PAL_HIGH);
    }
    chThdSleepMilliseconds(20);
  }
}

/*
 * One-time GPIO exercise performed in main() before the threads start:
 *  - configures the button as input with pull-up,
 *  - drives the LED through the pad level APIs and reads the level back.
 */
static void gpio_self_test(void) {

  /* Configuring the button pin as digital input with internal pull-up. */
  palSetLineMode(LINE_BUTTON, PAL_MODE_INPUT_PULLUP);

  /* Re-asserting the LED pin as push-pull output using palSetPadMode(). */
  palSetPadMode(GPIOA, GPIOA_LED_GREEN, PAL_MODE_OUTPUT_PUSHPULL);

  /* Driving the LED using the pad APIs and verifying the readback of the
     output level, a PAD write always reads back the ODR value. */
  for (unsigned i = 0U; i < 4U; i++) {
    palSetPad(GPIOA, GPIOA_LED_GREEN);
    if (palReadPad(GPIOA, GPIOA_LED_GREEN) != PAL_HIGH) {
      osalSysHalt("GPIO readback failure (set)");
    }
    chThdSleepMilliseconds(200);

    palClearPad(GPIOA, GPIOA_LED_GREEN);
    if (palReadPad(GPIOA, GPIOA_LED_GREEN) != PAL_LOW) {
      osalSysHalt("GPIO readback failure (clear)");
    }
    chThdSleepMilliseconds(200);
  }
}

/*
 * Application entry point.
 */
int main(void) {

  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  halInit();
  chSysInit();

  /* Configuring the LED line as push-pull output, initially off. */
  palSetLineMode(LINE_LED_GREEN, PAL_MODE_OUTPUT_PUSHPULL);
  palWriteLine(LINE_LED_GREEN, PAL_LOW);

  /* One-time pad API self test. */
  gpio_self_test();

  /*
   * Creating the GPIO test threads.
   */
  chThdCreateStatic(waBlinkThread, sizeof(waBlinkThread), NORMALPRIO,
                    BlinkThread, NULL);
  chThdCreateStatic(waButtonThread, sizeof(waButtonThread), NORMALPRIO,
                    ButtonThread, NULL);

  /*
   * Normal main() thread activity.
   */
  while (true) {
    chThdSleepMilliseconds(500);
  }
}
