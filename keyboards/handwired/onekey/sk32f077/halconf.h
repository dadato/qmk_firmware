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

/* The SK32F077 USB low level driver (hal_usb_lld.c) is compiled and linked by
 * this demo build (the QMK ChibiOS protocol layer requires the USB HAL types).
 * The USB device itself is not exercised: bring-up on hardware is a later
 * step, so no keyboard feature depends on a working USB connection here. */

#include_next <halconf.h>
