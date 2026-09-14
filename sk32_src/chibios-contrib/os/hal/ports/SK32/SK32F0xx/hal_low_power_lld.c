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

/**
 * @file    SK32F0xx/hal_low_power_lld.c
 * @brief   SK32F0xx low power subsystem low level driver source.
 *
 * @addtogroup LOWPOWER
 * @{
 */

#include "hal.h"

#if (SK32_HAL_USE_LOWPOWER == TRUE) || defined(__DOXYGEN__)

#include "hal_low_power_lld.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   PWR module low level driver initialization.
 * @note    Nothing to do: on process start the PWR module is enabled by the
 *          USB driver when the interface is brought up (rccEnablePWRInterface
 *          is called from usb_lld_start).  The STOP entry only touches the
 *          PWR->CR/CSR registers which are always clocked once enabled.
 *
 * @notapi
 */
#if defined(__DOXYGEN__)
void hal_low_power_lld_init(void);
#endif

void sk32_lowpower_stop_enter(void) {

  /* Clear the wakeup and standby flags so a residual flag cannot cause a
     spurious early wakeup on the next entry.  These are "write 1 to clear"
     bits on the STM32F0-class PWR. */
  PWR->CR |= (uint16_t)(PWR_CR_CWUF | PWR_CR_CSBF);

  /* Select Stop (deep sleep with regulator ON).  LPDS keeps the voltage
     regulator in low-power mode which is the lowest power Stop on the
     Cortex-M0 ST part; PDDS is cleared for Stop (not Standby). */
  PWR->CR &= (uint16_t)~(uint16_t)PWR_CR_PDDS;
  PWR->CR |= (uint16_t)PWR_CR_LPDS;

  /* Put the Cortex-M0 in deep-sleep on the next WFE/WFI. */
  SCB->SCR |= (uint32_t)SK32_SCR_SLEEPDEEP;

  /* Enter STOP: the core halts until an interrupt event (e.g. the USB
     RESUME interrupt whose enable is kept armed by the USB LLD). */
  __WFI();

  /* Wakeup: deep-sleep must be turned back off so the next normal wait uses
     regular sleep (or a subsequent STOP entry re-arms it explicitly). */
  SCB->SCR &= (uint32_t)~SK32_SCR_SLEEPDEEP;
}

void sk32_lowpower_stop_restore(void) {

  /* STOP drops the PLL/HSI system clock configuration; rebuild it exactly
     like right after reset.  This also re-establishes the 48 MHz reference
     needed by the USB peripheral and updates SystemCoreClock. */
  sk32_clock_init();
}

#endif /* (SK32_HAL_USE_LOWPOWER == TRUE) || defined(__DOXYGEN__) */

/** @} */