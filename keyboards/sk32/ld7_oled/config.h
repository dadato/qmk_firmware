#pragma once
/*qmk compile -kb sk32/ld7_oled -km default*/

/* LD7_OLED: 7 keys wired one GPIO per key, plus a 128x32 SSD1306 OLED and a
 * 7 LED WS2812 strip on the SK32F077.
 *
 * KEY1..KEY7 = PB12 PB11 PB10 PB2 PB1 PB0 PC5
 * The direct pin matrix (DIRECT_PINS / MATRIX_ROWS / MATRIX_COLS) is generated
 * from keyboard.json ("matrix_pins.direct" + "matrix_size"), column 0..6 in
 * the order above.
 */

/* Per-key eager debounce: KEY1..KEY7 are one-GPIO-per-key (DIRECT_PINS), so a
 * press is reported immediately and only the re-trigger guard uses the delay.
 * sym_eager_pk gives the shortest press latency for this wiring. */
#define DEBOUNCE_TYPE sym_eager_pk
#define DEBOUNCE 1

/*BOOTMAGIC KEY (KEY1)*/
#define BOOTMAGIC_LITE_ROW 0
#define BOOTMAGIC_LITE_COLUMN 0

/* SSD1306 128x32 OLED on I2C1: SDA = PB14, SCL = PB13 (alternate function 13,
 * open drain, external pull-ups).  Overrides the platform defaults PB6/PB7. */
#define I2C1_SCL_PIN B13
#define I2C1_SDA_PIN B14
#define I2C1_SCL_PAL_MODE 13
#define I2C1_SDA_PAL_MODE 13

/* The OLED is the default 128x32 (oled_driver.h falls back to it when no
 * OLED_DISPLAY_* size macro is defined) at address 0x3C on I2C1. */

/*RGB MATRIX*/
/* 7 LED WS2812 string driven by the SK32F077 SLED peripheral
 * (WS2812_DRIVER=sled).  The data pad is PC0 in alternate function 14, served
 * by the SLED2 channel registers (see platforms/chibios/drivers/ws2812_sled.c). */
#define WS2812_SLED_GROUP SLED2
#define SK32_SLED_USE_SLED2 TRUE
#define WS2812_SLED_PORT GPIOC
#define WS2812_SLED_PIN 0U
#define RGB_MATRIX_LED_COUNT 7

#define RGB_MATRIX_TIMEOUT 0 // number of milliseconds to wait until rgb automatically turns off
#define RGB_MATRIX_SLEEP    // turn off effects when suspended
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS 180 // limits maximum brightness of LEDs to 180 out of 255
#define RGB_MATRIX_DEFAULT_HUE 0          // Sets the default hue value, if none has been set
#define RGB_MATRIX_DEFAULT_SAT 255        // Sets the default saturation value, if none has been set
#define RGB_MATRIX_DEFAULT_VAL 180        // Sets the default brightness value, if none has been set
#define RGB_MATRIX_DEFAULT_SPD 127        // Sets the default animation speed, if none has been set
#define RGB_MATRIX_DEFAULT_MODE RGB_MATRIX_SOLID_COLOR // Sets the default mode, if none has been set

/*other rgb set*/
#define RGB_MATRIX_KEYPRESSES // reacts to keypresses
#define RGB_MATRIX_KEYRELEASES // reacts to keyreleases (instead of keypresses)

/*RGB MATRIX EFFECTS ON*/
#define ENABLE_RGB_MATRIX_BREATHING              //Enables RGB_MATRIX_BREATHING
#define ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT       //Enables RGB_MATRIX_CYCLE_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE  //Enables RGB_MATRIX_SOLID_REACTIVE_SIMPLE
