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

#pragma once

/* SK32F077 HAL configuration override for the QMK ChibiOS build.  The
 * platform common halconf.h enables the USB device driver used by the QMK
 * HID keyboard; the settings below only add bring-up test extras. */

#include_next <halconf.h>

/* Production firmware keeps the default HAL configuration (HAL_USE_SERIAL is
 * FALSE, no USB trace): the onekey only needs the USB device driver, which is
 * enabled by the common platform halconf.h.
 *
 * Bring-up mode (rules.mk: SK32_BRINGUP_TESTS=yes) additionally enables:
 *   - HAL_USE_SERIAL: USART1 (PA0=TX / PA1=RX, alternate function 10) used by
 *     the 'alive' heartbeat / trace-drain test thread;
 *   - SK32_USB_TRACE: the ISR event ring recorder inside the SK32 USB low
 *     level driver (hal_usb_lld.c).  Remove/disables once bring-up is done.
 *
 * The native SK32 I2C1 driver (hal_i2c_lld.c) is compiled out by default:
 * it is only included when both HAL_USE_I2C and SK32_I2C_USE_I2C1 are set to
 * TRUE (see the GENERIC_SK32_F077 board mcuconf.h). */
#if defined(SK32_BRINGUP_TESTS)
#    undef HAL_USE_SERIAL
#    define HAL_USE_SERIAL TRUE

#    define SK32_USB_TRACE 1
#endif /* SK32_BRINGUP_TESTS */
