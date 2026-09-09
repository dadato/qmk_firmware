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
/* SLED1/SLED2 frame test on the GENERIC_SK32_F077 board.                    */
/*===========================================================================*/

/*
 * Output pads used by this test:
 *
 *  - PB8 = SLED0_CH0 output pad (datasheet pad naming, alternate function
 *    14).  This is the pad driven by the 3Think RGBKeyboard reference
 *    project (User/Src/sled.c: PB8, GPIO_AF_14) through the SLED1 channel
 *    registers (DR[0]/DMAEN1/RSTSTR1, DMA1 channel 1, request SLED_G1),
 *    so it is used here as the reference group: if the driver and the
 *    timing configuration are right, the vendor proven pad must show a
 *    frame waveform.
 *  - PC0 = SLED1_CH0 output pad (datasheet pad naming, alternate function
 *    14).  The datasheet names the two pad groups SLED0_CHx / SLED1_CHx
 *    while the registers name the data paths "SLED1 channel" (DR[0],
 *    DMAEN1, RSTSTR1, request SLED_G1) and "SLED2 channel" (DR[1], DMAEN2,
 *    RSTSTR2, request SLED_G2): as the vendor drives the SLED0_CHx pad
 *    PB8 through DR[0], the SLED1_CHx pads (PC0..PC3) are served by the
 *    SLED2 channel registers (DR[1]/DMAEN2/RSTSTR2).  The frames of this
 *    group are therefore sent through the SLED2 group.
 *  - PB14 / PB15: two board LEDs used as per-group status indicators
 *    (PB14 = SLED1 group on PB8, PB15 = SLED2 group on PC0).
 *
 * A WS2812/SK6812 string (at least one LED) must be wired to the pad under
 * test, or the pad itself probed with a scope/logic analyzer: the SLED
 * block serializes every data byte into one channel byte, so the three byte
 * frame below programs a single LED (wire order GRB, no bit packing).
 *
 * Test procedure:
 *  - Both pads are routed to alternate function 14 and the SLED subsystem
 *    is started with the vendor RGBKeyboardSTK time codes (T0H=4, T1H=15,
 *    TRST=80 cycles, clock /4, baud /32).
 *  - Every ~200 ms a one LED frame is sent through @p sled_lld_send_bytes()
 *    on the SLED2 group (PC0) followed by one on the SLED1 group (PB8).
 *    The function is synchronous and returns @p MSG_OK only when the whole
 *    frame including the reset pulse has been shifted out.
 *  - The LED of a group is toggled after every successful frame and driven
 *    solid HIGH after the first timeout of that group, so a healthy group
 *    blinks while a failing one is steady ON (the test never halts, both
 *    pads keep being driven so the waveforms can be observed).
 */

/*
 * Application entry point.
 */
int main(void) {

  /* One WS2812 color frame (3 bytes, GRB wire order) programmed with the
     vendor time codes used by the 3Think RGBKeyboard reference. */
  static const SLEDConfig sled_cfg = {
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
  uint8_t frame_pb8[3];
  uint8_t frame_pc0[3];
  unsigned phase = 0U;
  bool     led_pb8  = false;
  bool     led_pc0  = false;

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
   * Output pad setup on alternate function 14 (the AF used by the vendor
   * RGBKeyboard sled.c on PB8):
   *  - PB8 = SLED0_CH0, served by the SLED1 channel registers (DR[0]).
   *  - PC0 = SLED1_CH0, served by the SLED2 channel registers (DR[1]).
   * The SLED low level driver does not configure the pads, so both are
   * routed here before the subsystem is started.
   */
  palSetPadMode(GPIOB, 8U, PAL_MODE_ALTERNATE(14) |
                           PAL_SK32_OSPEED_HIGHEST);
  palSetPadMode(GPIOC, 0U, PAL_MODE_ALTERNATE(14) |
                           PAL_SK32_OSPEED_HIGHEST);

  /*
   * Starting the SLED subsystem: the native low level driver programs the
   * TCR/CDR time codes, allocates the DMA1 channels of the enabled groups
   * (channel 1 for SLED1, channel 2 for SLED2), routes them to the SLED
   * request lines through the SYSCFG CFGR3 remapping (SLED_G1/SLED_G2) and
   * enables the block.  Both groups must be compiled in: SLED1 by default
   * and SLED2 through SK32_SLED_USE_SLED2=TRUE (see the Makefile).
   */
  sled_lld_init();
  sled_lld_start(&sled_cfg);

  /*
   * Running the frame test forever.  Each 200 ms iteration programs one LED
   * per group; PB14 blinks when the SLED1 (PB8) frames are acknowledged,
   * PB15 blinks when the SLED2 (PC0) frames are acknowledged, a steady ON
   * LED marks a group whose last frame timed out.
   */
  while (true) {
    /* SLED2 group frame on PC0: color = phase (GRB).*/
    frame_pc0[0] = colors[phase][0];
    frame_pc0[1] = colors[phase][1];
    frame_pc0[2] = colors[phase][2];
    if (sled_lld_send_bytes(SLED2, frame_pc0, sizeof(frame_pc0)) == MSG_OK) {
      if (led_pc0) {
        palClearPad(GPIOB, GPIOB_PIN15);
        led_pc0 = false;
      }
      else {
        palSetPad(GPIOB, GPIOB_PIN15);
        led_pc0 = true;
      }
    }
    else {
      /* Frame not acknowledged: keep the LED solid ON as an error marker
         and continue testing (the driver may recover on the next call).*/
      palSetPad(GPIOB, GPIOB_PIN15);
      led_pc0 = true;
    }

    /* SLED1 group frame on PB8: color = phase + 1 (GRB).*/
    frame_pb8[0] = colors[(phase + 1U) % 3U][0];
    frame_pb8[1] = colors[(phase + 1U) % 3U][1];
    frame_pb8[2] = colors[(phase + 1U) % 3U][2];
    if (sled_lld_send_bytes(SLED1, frame_pb8, sizeof(frame_pb8)) == MSG_OK) {
      if (led_pb8) {
        palClearPad(GPIOB, GPIOB_PIN14);
        led_pb8 = false;
      }
      else {
        palSetPad(GPIOB, GPIOB_PIN14);
        led_pb8 = true;
      }
    }
    else {
      palSetPad(GPIOB, GPIOB_PIN14);
      led_pb8 = true;
    }

    phase = (phase + 1U) % 3U;
    chThdSleepMilliseconds(200);
  }
}
