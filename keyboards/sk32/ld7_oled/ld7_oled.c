#include "ld7_oled.h"

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
