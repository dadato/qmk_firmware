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
/* USART1 PA0(PA1) TX/RX test on the GENERIC_SK32_F077 board.                */
/*===========================================================================*/

/*
 * Test resources used by this test:
 *
 *  - PA0 (USART1_TX, alternate function 10) and PA1 (USART1_RX, alternate
 *    function 10).  The pads are wired to a USB serial tool running at
 *    921600 8N1.
 *  - PB14 / PB15: two board LEDs wired to push-pull outputs, the PAL
 *    abstraction drives them high (PAL_HIGH) to switch the LEDs on.
 *
 * Test procedure:
 *  - A banner line "SK32-PA0-USART1-921600 seq=NNNN" is transmitted over
 *    SD1 (USART1, 921600 8N1) every 500 ms so the board can be observed on
 *    the serial tool.  Every received byte (typed on the tool, arriving on
 *    PA1) is echoed back to prove the RX path as well.
 *  - PB14 blinks at 1 Hz while the test is running; PB15 is toggled once
 *    per transmitted banner.
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

/* Transmits the periodic banner on SD1. */
static THD_WORKING_AREA(waBannerThread, 256);
static THD_FUNCTION(BannerThread, arg) {
  (void)arg;
  static const char pre[] = "SK32-PA0-USART1-921600 seq=";
  char buf[sizeof(pre) + 8U];
  uint32_t seq = 0;

  chRegSetThreadName("uart-banner");
  while (true) {
    size_t p;
    size_t nd;
    uint32_t v;
    char digits[8];

    /* Copying the prefix into the output buffer. */
    for (p = 0; pre[p] != '\0'; p++) {
      buf[p] = pre[p];
    }

    /* Appending the sequence number in decimal. */
    v = seq;
    nd = 0;
    do {
      digits[nd++] = (char)('0' + (v % 10U));
      v /= 10U;
    } while (v != 0U);
    while (nd > 0U) {
      buf[p++] = digits[--nd];
    }
    buf[p++] = '\r';
    buf[p++] = '\n';

    /* Transmitting the banner, non blocking with a timeout. */
    sdWriteTimeout(&SD1, (const uint8_t *)buf, p, TIME_MS2I(100));

    /* LED heartbeat per banner. */
    palTogglePad(GPIOB, GPIOB_PIN15);

    seq++;
    chThdSleepMilliseconds(500);
  }
}

/* Echoes back every byte received on SD1. */
static THD_WORKING_AREA(waEchoThread, 256);
static THD_FUNCTION(EchoThread, arg) {
  (void)arg;
  uint8_t b;

  chRegSetThreadName("uart-echo");
  while (true) {
    if (sdReadTimeout(&SD1, &b, 1U, TIME_MS2I(1000)) == 1U) {
      sdWriteTimeout(&SD1, &b, 1U, TIME_MS2I(100));
    }
  }
}

/*
 * Application entry point.
 */
int main(void) {

  static const SerialConfig usart1_cfg = {
    .speed = 921600,
    .cr1   = 0,                    /* 8 data bits, no parity, no OVER8.      */
    .cr2   = USART_CR2_STOP1_BITS, /* One stop bit.                          */
    .cr3   = 0
  };

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
   * Creating the working threads.
   */
  chThdCreateStatic(waBlinkThread, sizeof(waBlinkThread), NORMALPRIO,
                    BlinkThread, NULL);
  chThdCreateStatic(waBannerThread, sizeof(waBannerThread), NORMALPRIO,
                    BannerThread, NULL);
  chThdCreateStatic(waEchoThread, sizeof(waEchoThread), NORMALPRIO,
                    EchoThread, NULL);

  /*
   * USART1 pads setup on alternate function 10:
   *  - PA0 = USART1_TX, push-pull output.
   *  - PA1 = USART1_RX, input connected to the peripheral through the
   *    alternate function; an internal pull-up keeps the line quiet when the
   *    serial tool TX is not connected.
   */
  palSetPadMode(GPIOA, 0U, PAL_MODE_ALTERNATE(10) |
                           PAL_SK32_OSPEED_MID);
  palSetPadMode(GPIOA, 1U, PAL_MODE_ALTERNATE(10) |
                           PAL_SK32_OSPEED_MID |
                           PAL_SK32_PUPDR_PULLUP);

  /* Starting the serial driver (enables the USART1 clock and the NVIC
     vector, then programs BRR/CR1/CR2/CR3). */
  sdStart(&SD1, &usart1_cfg);

  /*
   * Normal main() thread activity.
   */
  while (true) {
    chThdSleepMilliseconds(1000);
  }
}
