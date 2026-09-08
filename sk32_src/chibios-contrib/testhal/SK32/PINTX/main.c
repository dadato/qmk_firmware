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
/* USART1 TX pad scan on the GENERIC_SK32_F077 board.                        */
/*===========================================================================*/

/*
 * Purpose:  while USART1 continuously shifts out 0x55 bytes, several pads are
 *           all mapped to alternate function 10 at the same time.  The pads
 *           that actually carry USART1_TX then toggle; the others stay quiet.
 *           The pad levels are observed from the debugger by reading GPIOA/GPIOB
 *           IDR only (no SWD register write needed at runtime).
 *
 * Candidates: PA0, PA1 (user claim), PA9, PA10 (official SDK example) and
 *             PB5, PB6 (pair verified by the earlier loopback SERIAL test).
 *             PA13/PA14 are kept untouched because they are the SWD lines.
 *
 *  - PB14 blinks at 1 Hz while the test is running.
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

/* Feeds 0x55 bytes into SD1 as fast as the driver accepts them. */
static THD_WORKING_AREA(waTxThread, 256);
static THD_FUNCTION(TxThread, arg) {
  (void)arg;
  static const uint8_t c = 0x55;

  chRegSetThreadName("uart-stream");
  while (true) {
    sdWriteTimeout(&SD1, &c, 1U, TIME_INFINITE);
  }
}

int main(void) {

  static const SerialConfig usart1_cfg = {
    .speed = 921600,
    .cr1   = 0,
    .cr2   = USART_CR2_STOP1_BITS,
    .cr3   = 0
  };

  halInit();
  chSysInit();

  /* PB14/PB15 LEDs as push-pull outputs, initially off. */
  palSetPadMode(GPIOB, GPIOB_PIN14, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOB, GPIOB_PIN15, PAL_MODE_OUTPUT_PUSHPULL);
  palClearPad(GPIOB, GPIOB_PIN14);
  palClearPad(GPIOB, GPIOB_PIN15);

  /* All candidate pads are mapped to USART1 alternate function 10. */
  palSetPadMode(GPIOA, 0U,  PAL_MODE_ALTERNATE(10) | PAL_SK32_OSPEED_MID);
  palSetPadMode(GPIOA, 1U,  PAL_MODE_ALTERNATE(10) | PAL_SK32_OSPEED_MID |
                           PAL_SK32_PUPDR_PULLUP);
  palSetPadMode(GPIOA, 9U,  PAL_MODE_ALTERNATE(10) | PAL_SK32_OSPEED_MID);
  palSetPadMode(GPIOA, 10U, PAL_MODE_ALTERNATE(10) | PAL_SK32_OSPEED_MID |
                           PAL_SK32_PUPDR_PULLUP);
  palSetPadMode(GPIOB, 5U,  PAL_MODE_ALTERNATE(10) | PAL_SK32_OSPEED_MID);
  palSetPadMode(GPIOB, 6U,  PAL_MODE_ALTERNATE(10) | PAL_SK32_OSPEED_MID |
                           PAL_SK32_PUPDR_PULLUP);

  /* Starting the serial driver (USART1 clock, NVIC and BRR setup). */
  sdStart(&SD1, &usart1_cfg);

  chThdCreateStatic(waBlinkThread, sizeof(waBlinkThread), NORMALPRIO,
                    BlinkThread, NULL);
  chThdCreateStatic(waTxThread, sizeof(waTxThread), NORMALPRIO,
                    TxThread, NULL);

  /* The main thread only idles. */
  while (true) {
    chThdSleepMilliseconds(1000);
  }
}
