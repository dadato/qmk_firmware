/*
    ChibiOS - Copyright (C) 2006-2026 Giovanni Di Sirio.
    SK32F0xx USB LLD - 3Think SK32F077 (Mentor/MUSB musbfsfc class USB FS controller)

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
 * @file    SK32F0xx/hal_usb_lld.c
 * @brief   SK32 USB subsystem low level driver source.
 * @details The SK32F077 embeds a Mentor/MUSB-class (musbfsfc) full speed
 *          USB device controller. Its register file (all 8 bit registers,
 *          endpoint register windows selected through the INDEX register and
 *          direct 8 bit FIFO windows) is completely different from the
 *          STM32F0 USB-PMA peripheral used on the STM32F072 the chip is
 *          otherwise compatible with. This driver implements the standard
 *          ChibiOS USB low level driver API on top of that controller.
 *
 *          The register level programming sequences mirror the 3Think
 *          CherryUSB port (usb_dc_musbfsfc.c) shipped in the
 *          "SK32F0xx_Firmware Package": the controller is used in
 *          "manual" mode (no AUTOSET/AUTOCLR), each IN/OUT transaction is
 *          serviced in the single USB interrupt (vector 31).
 *
 *          Control (EP0) transfers are entirely driven by the high level
 *          state machine in hal_usb.c through the regular
 *          usbStartTransmitI()/usbStartReceiveI() calls (software status
 *          stage, USB_EP0_STATUS_STAGE_SW); this driver only translates the
 *          controller events into the expected *_cb invocations.
 *
 * @addtogroup USB
 * @{
 */

#include <string.h>

#include "hal.h"

#if HAL_USE_USB || defined(__DOXYGEN__)

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/**
 * @brief   INTRIN1/INTRIN1E bit of EP0.
 */
#define SK32_EP0_IRQ_BIT               0x01U

/**
 * @brief   PWR->CR bits controlling the internal USB PHY LDO.
 * @note    These bits are a SK32 extension and are not defined in the
 *          standard STM32F072 CMSIS header.
 */
#define SK32_PWR_CR_LDO_VS_MASK        (3U << 18U)  /**< PHY voltage range.   */
#define SK32_PWR_CR_LDO_SEL            (1U << 17U)  /**< 0=VDD, 1=LDO.        */
#define SK32_PWR_CR_LDO_EN             (1U << 16U)  /**< LDO enable.          */
/**< 3.3V range, supply from the internal LDO, LDO enabled. */
#define SK32_PWR_CR_PHY_3V3_LDO        (SK32_PWR_CR_LDO_EN | SK32_PWR_CR_LDO_SEL)

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/** @brief USB1 driver identifier.*/
#if SK32_USB_USE_USB1 || defined(__DOXYGEN__)
USBDriver USBD1;
#endif

/*===========================================================================*/
/* Driver local variables and types.                                         */
/*===========================================================================*/

/**
 * @brief   EP0 state.
 * @note    It is an union because IN and OUT endpoints are never used at the
 *          same time for EP0.
 */
static union {
  /**
   * @brief   IN EP0 state.
   */
  USBInEndpointState in;
  /**
   * @brief   OUT EP0 state.
   */
  USBOutEndpointState out;
} ep0_state;

/**
 * @brief   Buffer for the EP0 setup packets.
 */
static uint8_t ep0setup_buffer[8];

/**
 * @brief   EP0 initialization structure.
 */
static const USBEndpointConfig ep0config = {
  .ep_mode          = USB_EP_MODE_TYPE_CTRL,
  .setup_cb         = _usb_ep0setup,
  .in_cb            = _usb_ep0in,
  .out_cb           = _usb_ep0out,
  .in_maxsize       = 0x40U,
  .out_maxsize      = 0x40U,
  .in_state         = &ep0_state.in,
  .out_state        = &ep0_state.out,
  .ep_buffers       = 1U,
  .setup_buf        = ep0setup_buffer
};

/**
 * @brief   Set when an EP0 SETUP packet is being served by the ISR and the
 *          setup data has still to be popped from the FIFO by the high level
 *          layer (usb_lld_read_setup()). The OPKTRD service must be performed
 *          after the FIFO pop, exactly like the vendor driver does.
 */
static bool ep0_setup_pending = false;

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   Selects an endpoint register window through the INDEX register.
 *
 * @param[in] ep        endpoint number (0..7)
 * @return              The previous INDEX register value.
 *
 * @notapi
 */
static uint8_t usb_ep_select(uint8_t ep) {
  uint8_t old_index;

  old_index = SK32_USB->INDEX;
  SK32_USB->INDEX = ep;
  return old_index;
}

/**
 * @brief   Writes a packet payload into an endpoint FIFO.
 *
 * @param[in] ep        endpoint number
 * @param[in] buf       buffer where to fetch the packet data
 * @param[in] n         number of bytes to write
 *
 * @notapi
 */
static void usb_fifo_write(uint8_t ep, const uint8_t *buf, size_t n) {

  /* The FIFO windows are directly addressed, no INDEX selection needed.*/
  while (n > 0U) {
    SK32_USB->FIFO[ep] = *buf++;
    n--;
  }
}

/**
 * @brief   Reads a received packet payload from an endpoint FIFO.
 *
 * @param[in] ep        endpoint number
 * @param[in] buf       buffer where to copy the packet data
 * @param[in] n         number of bytes to read
 *
 * @notapi
 */
static void usb_fifo_read(uint8_t ep, uint8_t *buf, size_t n) {

  /* The FIFO windows are directly addressed, no INDEX selection needed.*/
  while (n > 0U) {
    *buf++ = SK32_USB->FIFO[ep];
    n--;
  }
}

/**
 * @brief   Reads the received packet byte count of the selected endpoint.
 *
 * @return              The received byte count.
 *
 * @notapi
 */
static size_t usb_get_rx_count(void) {

  return (size_t)SK32_USB->COUNT |
         ((size_t)SK32_USB->OUTCOUNT2 << 8U);
}

/**
 * @brief   Common ISR code, IN endpoint transmission.
 * @details Invoked when an IN packet previously loaded with IPKTRD has been
 *          transmitted on the bus. If the transfer has not been completed
 *          the next packet is loaded, otherwise the IN callback is invoked.
 * @pre     The INDEX register must select the endpoint.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 *
 * @notapi
 */
static void usb_serve_in(USBDriver *usbp, usbep_t ep) {
  size_t n;
  const USBEndpointConfig *epcp = usbp->epc[ep];
  USBInEndpointState *isp = epcp->in_state;

  /* A transmit underrun condition is acknowledged (IN endpoints only, the
     CSR register of EP0 is the CSR0 register with a different bit map).*/
  if (ep != 0U) {
    if ((SK32_USB->CSR & SK32_INCSR1_UNDERRUN) != 0U) {
      SK32_USB->CSR &= (uint8_t)~SK32_INCSR1_UNDERRUN;
    }
  }

  /* Only meaningful if the high level layer has a transmit pending.*/
  if ((usbp->transmitting & (uint16_t)((unsigned)1U << (unsigned)ep)) == 0U) {
    return;
  }

  /* Accumulating the size of the packet whose transmission has just been
     acknowledged by the controller.*/
  isp->txcnt += isp->txlast;
  n = isp->txsize - isp->txcnt;
  if (n > 0U) {
    /* Transfer not completed, loading the next packet.*/
    if (n > (size_t)epcp->in_maxsize) {
      n = (size_t)epcp->in_maxsize;
    }
    isp->txbuf += isp->txlast;
    isp->txlast = n;
    usb_fifo_write(ep, isp->txbuf, n);

    if (ep == 0U) {
      /* EP0: a short IN packet terminates the data stage.*/
      if (n < (size_t)epcp->in_maxsize) {
        SK32_USB->CSR |= SK32_CSR0_DATEND;
      }
      SK32_USB->CSR |= SK32_CSR0_IPKTRD;
    }
    else {
      SK32_USB->CSR |= SK32_INCSR1_IPKTRD;
    }
  }
  else {
    /* Transfer completed, invoking the callback.*/
    _usb_isr_invoke_in_cb(usbp, ep);
  }
}

/**
 * @brief   Common ISR code, OUT endpoint reception.
 * @details Invoked when an OUT packet has been received (OPKTRD set). The
 *          packet is read from the FIFO and the receive operation is
 *          re-armed; the OUT callback is invoked when the transfer is
 *          complete (short packet received or expected packet count reached).
 * @pre     The INDEX register must select the endpoint.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 *
 * @notapi
 */
static void usb_serve_out(USBDriver *usbp, usbep_t ep) {
  size_t n;
  const USBEndpointConfig *epcp = usbp->epc[ep];
  USBOutEndpointState *osp = epcp->out_state;

  /* Endpoint 0 uses the CSR0 register, the other endpoints the OUTCSR1
     register, but the OPKTRD flag is bit 0 in both cases.*/
  if (ep == 0U) {
    if ((SK32_USB->CSR & SK32_CSR0_OPKTRD) == 0U) {
      return;
    }
  }
  else {
    if ((SK32_USB->OUTCSR1 & SK32_OUTCSR1_OPKTRD) == 0U) {
      return;
    }
  }

  /* Unsolicited packet, no high level receive in progress: the packet is
     discarded and the endpoint re-armed to avoid locking out the host.*/
  if ((usbp->receiving & (uint16_t)((unsigned)1U << (unsigned)ep)) == 0U) {
    if (ep == 0U) {
      SK32_USB->CSR |= SK32_CSR0_SEROPKTRD;
    }
    else {
      SK32_USB->OUTCSR1 &= (uint8_t)~SK32_OUTCSR1_OPKTRD;
    }
    return;
  }

  /* Reading the received packet from the FIFO.*/
  n = usb_get_rx_count();
  if (n > 0U) {
    usb_fifo_read(ep, osp->rxbuf, n);
  }

  /* Servicing OPKTRD, this re-enables the reception of the next packet.*/
  if (ep == 0U) {
    SK32_USB->CSR |= SK32_CSR0_SEROPKTRD;
  }
  else {
    SK32_USB->OUTCSR1 &= (uint8_t)~SK32_OUTCSR1_OPKTRD;
  }

  /* Transaction data updated.*/
  osp->rxbuf += n;
  osp->rxcnt  += n;
  osp->rxsize -= n;
  osp->rxpkts -= 1U;

  /* The transaction is completed if the expected number of packets has been
     received or the current packet is a short packet.*/
  if ((n < (size_t)epcp->out_maxsize) || (osp->rxpkts == 0U)) {
    /* Transfer complete, invoking the callback.*/
    _usb_isr_invoke_out_cb(usbp, ep);
  }
}

/**
 * @brief   Handles an EP0 SETUP packet.
 * @details The FIFO contains the 8 bytes setup packet. The high level setup
 *          callback is invoked; the callback pops the setup data through
 *          usbReadSetup()/usb_lld_read_setup() which also performs the
 *          OPKTRD service (the packet must be read before being serviced,
 *          exactly as done by the vendor driver).
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 *
 * @notapi
 */
static void usb_serve_ep0_setup(USBDriver *usbp) {

  ep0_setup_pending = true;
  _usb_isr_invoke_setup_cb(usbp, 0);

  /* The setup callback did not consume the packet, discarding it.*/
  if (ep0_setup_pending) {
    SK32_USB->CSR |= SK32_CSR0_SEROPKTRD;
    ep0_setup_pending = false;
  }
}

/**
 * @brief   Common ISR code for EP0.
 * @details EP0 events (SETUP packets, control data packets and IN packet
 *          transmission completions) are all reported on the INTRIN1 bit 0.
 *          The CSR0 flags and the high level EP0 state machine are used to
 *          tell the event kinds apart.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 *
 * @notapi
 */
static void usb_serve_ep0(USBDriver *usbp) {
  uint8_t csr;
  uint8_t old_index;
  bool setup_aborted;

  old_index = usb_ep_select(0);
  csr = SK32_USB->CSR;

  /* SETUPEND is set when a new SETUP packet has aborted an ongoing control
     transfer before DATAEND. It must be serviced and it also tells us that
     the pending packet is a SETUP packet even if the high level state
     machine is still inside the previous (aborted) transfer.*/
  setup_aborted = (csr & SK32_CSR0_SETUPEND) != 0U;
  if (setup_aborted) {
    SK32_USB->CSR |= SK32_CSR0_SERSETUPEND;
  }

  /* A STALL handshake has been sent, clearing the status flag.*/
  if ((csr & SK32_CSR0_SENTST) != 0U) {
    SK32_USB->CSR &= (uint8_t)~SK32_CSR0_SENTST;
  }

  if ((csr & SK32_CSR0_OPKTRD) != 0U) {
    /* A packet has been received on EP0: it is a SETUP packet when the high
       level state machine is waiting for one, when it is in the error state
       (a STALL has been sent and a new transfer is starting) or when the
       previous transfer has been aborted. Otherwise it is a data/status
       packet of a control write.*/
    if (setup_aborted ||
        (usbp->ep0state == USB_EP0_STP_WAITING) ||
        (usbp->ep0state == USB_EP0_ERROR)) {
      usb_serve_ep0_setup(usbp);
    }
    else {
      usb_serve_out(usbp, 0);
    }
  }
  else {
    /* No packet pending: the event must be the transmission completion of
       an IN packet previously loaded with IPKTRD.*/
    usb_serve_in(usbp, 0);
  }

  SK32_USB->INDEX = old_index;
}

/*===========================================================================*/
/* Driver interrupt handlers.                                                */
/*===========================================================================*/

#if SK32_USB_USE_USB1 || defined(__DOXYGEN__)
/**
 * @brief   USB interrupt handler.
 * @note    The SK32F077 has a single USB IRQ line (vector 31); the HP and LP
 *          STM32F0 interrupt naming conventions both map onto this vector.
 *
 * @isr
 */
OSAL_IRQ_HANDLER(SK32_USB_HANDLER) {
  uint8_t is;
  uint8_t txis;
  uint8_t rxis;
  uint8_t old_index;
  uint8_t ep;
  USBDriver *usbp = &USBD1;

  OSAL_IRQ_PROLOGUE();

  /* Reading the common USB interrupt flags.*/
  is = SK32_USB->INTRUSB;

  /* USB bus reset handling: the controller is brought back to a known state
     and the high level driver is reset (this re-initializes EP0).*/
  if ((is & SK32_INTRUSB_RESET) != 0U) {
    old_index = SK32_USB->INDEX;

    /* Only EP0 remains enabled, all the other endpoint interrupt sources
       are disabled.*/
    SK32_USB->INTRIN1E  = SK32_EP0_IRQ_BIT;
    SK32_USB->INTRIN2E  = 0x00U;
    SK32_USB->INTROUT1E = 0x00U;
    SK32_USB->INTROUT2E = 0x00U;

    /* Deconfiguring the endpoints and clearing their residual state.*/
    for (ep = 1U; ep < (uint8_t)USB_MAX_ENDPOINTS; ep++) {
      SK32_USB->INDEX = ep;
      SK32_USB->INMAXP  = 0U;
      SK32_USB->OUTMAXP = 0U;
      SK32_USB->INCSR2  = 0U;
      SK32_USB->OUTCSR2 = 0U;
      SK32_USB->CSR     = 0U;
      SK32_USB->OUTCSR1 = 0U;
    }

    SK32_USB->INDEX = old_index;

    /* High level reset: clears the driver state and calls usb_lld_reset().*/
    _usb_reset(usbp);

    OSAL_IRQ_EPILOGUE();
    return;
  }

  /* USB bus SUSPEND condition handling.*/
  if ((is & SK32_INTRUSB_SUS) != 0U) {
    _usb_suspend(usbp);
  }

  /* USB bus RESUME condition handling.*/
  if ((is & SK32_INTRUSB_RESUME) != 0U) {
    _usb_wakeup(usbp);
  }

  /* SOF handling.*/
  if ((is & SK32_INTRUSB_SOF) != 0U) {
    _usb_isr_invoke_sof_cb(usbp);
  }

  /* Reading the endpoint interrupt flags and masking them with the enables.
     EP0 and the IN endpoints are reported by INTRIN1 while the OUT endpoints
     (EP1..7) are reported by INTROUT1. Note that EP0 events (both
     directions) are reported on INTRIN1 bit 0 and are serviced through the
     CSR0 register.*/
  txis = (uint8_t)(SK32_USB->INTRIN1 & SK32_USB->INTRIN1E);
  rxis = (uint8_t)(SK32_USB->INTROUT1 & SK32_USB->INTROUT1E);

  old_index = SK32_USB->INDEX;

  /* EP0 events.*/
  if ((txis & SK32_EP0_IRQ_BIT) != 0U) {
    usb_serve_ep0(usbp);
  }

  /* IN endpoints events.*/
  for (ep = 1U; ep < (uint8_t)USB_MAX_ENDPOINTS; ep++) {
    if ((txis & ((uint8_t)1U << ep)) != 0U) {
      if (usbp->epc[ep] != NULL) {
        SK32_USB->INDEX = ep;
        usb_serve_in(usbp, ep);
      }
    }
  }

  /* OUT endpoints events.*/
  for (ep = 1U; ep < (uint8_t)USB_MAX_ENDPOINTS; ep++) {
    if ((rxis & ((uint8_t)1U << ep)) != 0U) {
      if (usbp->epc[ep] != NULL) {
        SK32_USB->INDEX = ep;
        usb_serve_out(usbp, ep);
      }
    }
  }

  SK32_USB->INDEX = old_index;

  OSAL_IRQ_EPILOGUE();
}
#endif /* SK32_USB_USE_USB1 */

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Low level USB driver initialization.
 *
 * @notapi
 */
void usb_lld_init(void) {

  /* Driver initialization.*/
  ep0_setup_pending = false;
  usbObjectInit(&USBD1);
}

/**
 * @brief   Configures and activates the USB peripheral.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 *
 * @notapi
 */
void usb_lld_start(USBDriver *usbp) {

  if (usbp->state == USB_STOP) {
#if SK32_USB_USE_USB1
    if (&USBD1 == usbp) {
      uint32_t pwrcr;

      /* The musbfsfc controller requires a 48MHz reference clock. On the
         SK32F077 this clock is generated by the internal PLL48 which is
         routed to the USB peripheral by clearing the USBSW bit of CFGR3
         (see sk32f0xx.h).*/
      RCC->CR2 |= RCC_CR2_PLL48ON;
      while ((RCC->CR2 & RCC_CR2_PLL48RDY) == 0U) {
      }
      RCC->CFGR3 &= ~RCC_CFGR3_USBSW;

      /* Clocking the power interface to program the internal USB PHY.*/
      rccEnablePWRInterface(true);

      /* Programming the internal USB PHY supply: 3.3V range, powered by the
         internal LDO. These PWR->CR bits are a SK32 extension.*/
      pwrcr = PWR->CR & ~((uint32_t)SK32_PWR_CR_LDO_VS_MASK |
                          SK32_PWR_CR_LDO_EN);
      PWR->CR = pwrcr | (uint32_t)SK32_PWR_CR_PHY_3V3_LDO;

      /* Configuring PA11 (D-) and PA12 (D+) as push-pull outputs at 50MHz,
         as done by the vendor BSP. The USB PHY takes the pins over once the
         controller is enabled.*/
      rccEnableAHB(RCC_AHBENR_GPIOAEN, true);
      GPIOA->MODER   = (GPIOA->MODER & ~((uint32_t)0x3U << 22U) &
                        ~((uint32_t)0x3U << 24U)) |
                       ((uint32_t)0x1U << 22U) | ((uint32_t)0x1U << 24U);
      GPIOA->OTYPER  &= ~((uint32_t)0x3U << 11U);
      GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~((uint32_t)0x3U << 22U) &
                        ~((uint32_t)0x3U << 24U)) |
                       ((uint32_t)0x3U << 22U) | ((uint32_t)0x3U << 24U);
      GPIOA->PUPDR   &= ~((uint32_t)0x3U << 22U) &
                        ~((uint32_t)0x3U << 24U);

      /* USB peripheral clock activation and reset.*/
      rccEnableUSB(true);
      rccResetUSB();

      /* Enabling the shared USB IRQ vector (HP and LP naming conventions
         map onto the same vector 31 on this device).*/
      nvicEnableVector(SK32_USB_NUMBER,
                       SK32_USB_USB1_LP_IRQ_PRIORITY);

      /* Basic controller setup; the bus is not connected yet (the D+ pull-up
         is applied by usb_lld_connect_bus()).*/
      SK32_USB->FADDR = 0U;
      SK32_USB->PULL  = 0U;
      SK32_USB->POWER = SK32_POWER_ENSUS;
    }
#endif /* SK32_USB_USE_USB1 */

    /* Reset procedure enforced on driver start (initializes EP0).*/
    usb_lld_reset(usbp);
  }
}

/**
 * @brief   Deactivates the USB peripheral.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 *
 * @notapi
 */
void usb_lld_stop(USBDriver *usbp) {

  /* If in ready state then disables the USB clock.*/
  if (usbp->state != USB_STOP) {
#if SK32_USB_USE_USB1
    if (&USBD1 == usbp) {
      nvicDisableVector(SK32_USB_NUMBER);

      /* Disconnecting the bus and disabling all the interrupt sources.*/
      SK32_USB->PULL      = 0U;
      SK32_USB->POWER     = 0U;
      SK32_USB->INTRUSBE  = 0U;
      SK32_USB->INTRIN1E  = 0U;
      SK32_USB->INTRIN2E  = 0U;
      SK32_USB->INTROUT1E = 0U;
      SK32_USB->INTROUT2E = 0U;

      rccDisableUSB();
    }
#endif /* SK32_USB_USE_USB1 */
  }
}

/**
 * @brief   USB low level reset routine.
 * @details Called after a USB bus reset (from the ISR through _usb_reset())
 *          or on driver start.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 *
 * @notapi
 */
void usb_lld_reset(USBDriver *usbp) {
  uint8_t csr;
  uint8_t irq;
  uint8_t old_index;

  /* Post reset initialization.*/
  SK32_USB->FADDR = 0U;

  /* Suspend detection enabled (the suspend mode entry is reported through
     INTRUSB.SUS).*/
  SK32_USB->POWER |= SK32_POWER_ENSUS;

  /* Only EP0 is enabled; the other endpoints are enabled on demand by
     usb_lld_init_endpoint().*/
  SK32_USB->INTRIN1E  = SK32_EP0_IRQ_BIT;
  SK32_USB->INTRIN2E  = 0x00U;
  SK32_USB->INTROUT1E = 0x00U;
  SK32_USB->INTROUT2E = 0x00U;

  /* Common USB interrupts: SUSPEND, RESUME and RESET are always enabled.
     The SOF interrupt is only enabled if a callback is defined because it
     is a high rate source.*/
  irq = SK32_INTRUSBE_SUS | SK32_INTRUSBE_RESUME | SK32_INTRUSBE_RESET;
  if ((usbp->config != NULL) && (usbp->config->sof_cb != NULL)) {
    irq |= SK32_INTRUSBE_SOF;
  }
  SK32_USB->INTRUSBE = irq;

  /* Servicing any stale EP0 condition left by the bus reset.*/
  old_index = usb_ep_select(0);
  csr = SK32_USB->CSR;
  if ((csr & SK32_CSR0_OPKTRD) != 0U) {
    SK32_USB->CSR |= SK32_CSR0_SEROPKTRD;
  }
  if ((csr & SK32_CSR0_SETUPEND) != 0U) {
    SK32_USB->CSR |= SK32_CSR0_SERSETUPEND;
  }
  SK32_USB->INDEX = old_index;

  /* EP0 initialization.*/
  usbp->epc[0] = &ep0config;
  usb_lld_init_endpoint(usbp, 0U);
}

/**
 * @brief   Sets the USB address.
 * @details With USB_LATE_SET_ADDRESS the high level layer invokes this
 *          function once the SET_ADDRESS status stage has been completed,
 *          so the controller can safely latch the new function address.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 *
 * @notapi
 */
void usb_lld_set_address(USBDriver *usbp) {

  SK32_USB->FADDR = (uint8_t)(usbp->address & SK32_FADDR_FUNC_ADDR);
}

/**
 * @brief   Enables an endpoint.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 *
 * @notapi
 */
void usb_lld_init_endpoint(USBDriver *usbp, usbep_t ep) {
  const USBEndpointConfig *epcp = usbp->epc[ep];
  uint8_t old_index;

  old_index = usb_ep_select(ep);

  if (ep == 0U) {
    /* EP0 is a bidirectional control endpoint with a fixed 64 bytes packet
       size; only the interrupt source needs to be enabled.*/
    SK32_USB->INTRIN1E |= SK32_EP0_IRQ_BIT;
  }
  else {
    bool is_in  = epcp->in_state != NULL;
    bool is_out = epcp->out_state != NULL;

    /* The musbfsfc endpoints are unidirectional: an endpoint is configured
       as IN by setting INCSR2.MODE, otherwise it is an OUT endpoint. The
       same FIFO/register window cannot serve both directions (EP0 is the
       only bidirectional endpoint).*/
    osalDbgAssert(!(is_in && is_out),
                  "SK32 endpoint cannot be both IN and OUT");

    if (is_in) {
      /* IN endpoint configuration: max packet size expressed in words,
         direction set to IN, interrupt source enabled.*/
      SK32_USB->INMAXP = (uint8_t)((epcp->in_maxsize + 7U) >> 3U);
      SK32_USB->INCSR2 |= SK32_INCSR2_MODE;
      if ((epcp->ep_mode & USB_EP_MODE_TYPE) == USB_EP_MODE_TYPE_ISOC) {
        SK32_USB->INCSR2 |= SK32_INCSR2_ISO;
      }
      SK32_USB->INTRIN1E |= (uint8_t)(1U << ep);
    }
    if (is_out) {
      /* OUT endpoint configuration.*/
      SK32_USB->OUTMAXP = (uint8_t)((epcp->out_maxsize + 7U) >> 3U);
      SK32_USB->INCSR2 &= (uint8_t)~SK32_INCSR2_MODE;
      if ((epcp->ep_mode & USB_EP_MODE_TYPE) == USB_EP_MODE_TYPE_ISOC) {
        SK32_USB->OUTCSR2 |= SK32_OUTCSR2_ISO;
      }
      SK32_USB->INTROUT1E |= (uint8_t)(1U << ep);
    }
  }

  SK32_USB->INDEX = old_index;
}

/**
 * @brief   Disables all the active endpoints except the endpoint zero.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 *
 * @notapi
 */
void usb_lld_disable_endpoints(USBDriver *usbp) {
  uint8_t ep;
  uint8_t old_index;

  (void)usbp;

  old_index = usb_ep_select(0);

  /* Disabling all the endpoints except EP0.*/
  for (ep = 1U; ep < (uint8_t)USB_MAX_ENDPOINTS; ep++) {
    SK32_USB->INDEX = ep;
    SK32_USB->INTRIN1E &= (uint8_t)~(uint8_t)(1U << ep);
    SK32_USB->INTROUT1E &= (uint8_t)~(uint8_t)(1U << ep);
    SK32_USB->INMAXP  = 0U;
    SK32_USB->OUTMAXP = 0U;
    SK32_USB->INCSR2  = 0U;
    SK32_USB->OUTCSR2 = 0U;
    SK32_USB->CSR     = 0U;
    SK32_USB->OUTCSR1 = 0U;
  }

  SK32_USB->INDEX = old_index;
}

/**
 * @brief   Returns the status of an OUT endpoint.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 * @return              The endpoint status.
 * @retval EP_STATUS_DISABLED The endpoint is not active.
 * @retval EP_STATUS_STALLED  The endpoint is stalled.
 * @retval EP_STATUS_ACTIVE   The endpoint is active.
 *
 * @notapi
 */
usbepstatus_t usb_lld_get_status_out(USBDriver *usbp, usbep_t ep) {
  uint8_t old_index;
  uint8_t csr;
  usbepstatus_t status;

  if ((usbp->epc[ep] == NULL) || (usbp->epc[ep]->out_state == NULL)) {
    return EP_STATUS_DISABLED;
  }

  old_index = usb_ep_select(ep);
  if (ep == 0U) {
    csr = SK32_USB->CSR;
    status = ((csr & SK32_CSR0_SENDST) != 0U) ? EP_STATUS_STALLED :
                                                EP_STATUS_ACTIVE;
  }
  else {
    csr = SK32_USB->OUTCSR1;
    status = ((csr & SK32_OUTCSR1_SENDST) != 0U) ? EP_STATUS_STALLED :
                                                   EP_STATUS_ACTIVE;
  }
  SK32_USB->INDEX = old_index;

  return status;
}

/**
 * @brief   Returns the status of an IN endpoint.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 * @return              The endpoint status.
 * @retval EP_STATUS_DISABLED The endpoint is not active.
 * @retval EP_STATUS_STALLED  The endpoint is stalled.
 * @retval EP_STATUS_ACTIVE   The endpoint is active.
 *
 * @notapi
 */
usbepstatus_t usb_lld_get_status_in(USBDriver *usbp, usbep_t ep) {
  uint8_t old_index;
  uint8_t csr;
  usbepstatus_t status;

  if ((usbp->epc[ep] == NULL) || (usbp->epc[ep]->in_state == NULL)) {
    return EP_STATUS_DISABLED;
  }

  old_index = usb_ep_select(ep);
  if (ep == 0U) {
    csr = SK32_USB->CSR;
    status = ((csr & SK32_CSR0_SENDST) != 0U) ? EP_STATUS_STALLED :
                                                EP_STATUS_ACTIVE;
  }
  else {
    csr = SK32_USB->CSR;
    status = ((csr & SK32_INCSR1_SENDST) != 0U) ? EP_STATUS_STALLED :
                                                  EP_STATUS_ACTIVE;
  }
  SK32_USB->INDEX = old_index;

  return status;
}

/**
 * @brief   Reads a setup packet from the endpoint zero FIFO.
 * @details This function must be invoked in the context of the @p setup_cb
 *          callback in order to read the received setup packet. When invoked
 *          while an EP0 SETUP packet is being served by the ISR the OPKTRD
 *          service is performed right after the FIFO pop, mirroring the
 *          vendor driver sequence.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 * @param[out] buf      buffer where to copy the packet data
 *
 * @notapi
 */
void usb_lld_read_setup(USBDriver *usbp, usbep_t ep, uint8_t *buf) {
  uint8_t old_index;

  (void)usbp;
  (void)ep;

  /* Reading the 8 setup bytes from the EP0 FIFO window (direct access).*/
  usb_fifo_read(0U, buf, 8U);

  /* If a SETUP packet is currently being served by the ISR the OPKTRD flag
     must be serviced after the data has been popped from the FIFO.*/
  if (ep0_setup_pending) {
    old_index = usb_ep_select(0);
    SK32_USB->CSR |= SK32_CSR0_SEROPKTRD;
    SK32_USB->INDEX = old_index;
    ep0_setup_pending = false;
  }
}

/**
 * @brief   Starts a receive operation on an OUT endpoint.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 *
 * @notapi
 */
void usb_lld_start_out(USBDriver *usbp, usbep_t ep) {
  USBOutEndpointState *osp = usbp->epc[ep]->out_state;
  uint8_t old_index;

  /* Transfer initialization: the expected number of packets is computed so
     that the ISR can detect the end of the transaction also when the last
     packet is not a short packet.*/
  if (osp->rxsize == 0U) {
    /* Special case for zero sized packets (e.g. the EP0 status ZLP).*/
    osp->rxpkts = 1U;
  }
  else {
    osp->rxpkts = (uint16_t)((osp->rxsize +
                              usbp->epc[ep]->out_maxsize - 1U) /
                             usbp->epc[ep]->out_maxsize);
  }

  if (ep > 0U) {
    /* Enabling the OUT interrupt source. The endpoint itself is always
       ready to receive an OUT token once it has been initialized; unserviced
       packets are automatically NAKed by the controller when the FIFO is
       full, so no explicit VALID/NAK arm is required.*/
    old_index = usb_ep_select(ep);
    SK32_USB->INTROUT1E |= (uint8_t)(1U << ep);
    SK32_USB->INDEX = old_index;
  }
}

/**
 * @brief   Starts a transmit operation on an IN endpoint.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 *
 * @notapi
 */
void usb_lld_start_in(USBDriver *usbp, usbep_t ep) {
  size_t n;
  USBInEndpointState *isp = usbp->epc[ep]->in_state;
  uint8_t old_index;

  /* Transfer initialization: the first packet is written into the FIFO and
     the transmission is started with IPKTRD.*/
  n = isp->txsize;
  if (n > (size_t)usbp->epc[ep]->in_maxsize) {
    n = (size_t)usbp->epc[ep]->in_maxsize;
  }
  isp->txlast = n;

  if (n > 0U) {
    usb_fifo_write(ep, isp->txbuf, n);
  }

  old_index = usb_ep_select(ep);
  if (ep == 0U) {
    /* EP0: a zero length packet (status stage ZLP or terminating ZLP of the
       data stage) or a short IN packet carries DATAEND, ending the data
       stage.*/
    if (n < (size_t)usbp->epc[ep]->in_maxsize) {
      SK32_USB->CSR |= SK32_CSR0_DATEND;
    }
    SK32_USB->CSR |= SK32_CSR0_IPKTRD;
  }
  else {
    /* Regular IN endpoint: an empty FIFO with IPKTRD set transmits a zero
       length packet.*/
    SK32_USB->CSR |= SK32_INCSR1_IPKTRD;
  }
  SK32_USB->INDEX = old_index;
}

/**
 * @brief   Brings an OUT endpoint in the stalled state.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 *
 * @notapi
 */
void usb_lld_stall_out(USBDriver *usbp, usbep_t ep) {
  uint8_t csr;
  uint8_t old_index;

  (void)usbp;

  old_index = usb_ep_select(ep);
  if (ep == 0U) {
    /* Stalling EP0: if a packet is pending it must be dropped at the same
       time, otherwise the STALL handshake would be overridden by the packet
       acknowledgment.*/
    csr = SK32_USB->CSR;
    if ((csr & SK32_CSR0_OPKTRD) != 0U) {
      SK32_USB->CSR |= SK32_CSR0_SENDST | SK32_CSR0_SEROPKTRD;
    }
    else {
      SK32_USB->CSR |= SK32_CSR0_SENDST;
    }
  }
  else {
    SK32_USB->OUTCSR1 |= SK32_OUTCSR1_SENDST;
  }
  SK32_USB->INDEX = old_index;
}

/**
 * @brief   Brings an IN endpoint in the stalled state.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 *
 * @notapi
 */
void usb_lld_stall_in(USBDriver *usbp, usbep_t ep) {
  uint8_t old_index;

  (void)usbp;

  old_index = usb_ep_select(ep);
  if (ep == 0U) {
    SK32_USB->CSR |= SK32_CSR0_SENDST;
  }
  else {
    SK32_USB->CSR |= SK32_INCSR1_SENDST;
  }
  SK32_USB->INDEX = old_index;
}

/**
 * @brief   Brings an OUT endpoint in the active state.
 * @details Removes a possible STALL condition and resets the data toggle.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 *
 * @notapi
 */
void usb_lld_clear_out(USBDriver *usbp, usbep_t ep) {
  uint8_t old_index;

  (void)usbp;

  old_index = usb_ep_select(ep);
  if (ep == 0U) {
    /* EP0: removing the stall flags, the toggle is managed by the hardware
       and is reset by every SETUP packet.*/
    SK32_USB->CSR &= (uint8_t)~(SK32_CSR0_SENDST | SK32_CSR0_SENTST);
  }
  else {
    /* Clear the stall on the OUT endpoint and reset the data toggle.*/
    SK32_USB->OUTCSR1 &= (uint8_t)~(SK32_OUTCSR1_SENDST |
                                     SK32_OUTCSR1_SENTST);
    SK32_USB->OUTCSR1 |= SK32_OUTCSR1_CLRDT;
  }
  SK32_USB->INDEX = old_index;
}

/**
 * @brief   Brings an IN endpoint in the active state.
 * @details Removes a possible STALL condition and resets the data toggle.
 *
 * @param[in] usbp      pointer to the @p USBDriver object
 * @param[in] ep        endpoint number
 *
 * @notapi
 */
void usb_lld_clear_in(USBDriver *usbp, usbep_t ep) {
  uint8_t old_index;

  (void)usbp;

  old_index = usb_ep_select(ep);
  if (ep == 0U) {
    /* EP0: removing the stall flags, the toggle is managed by the hardware
       and is reset by every SETUP packet.*/
    SK32_USB->CSR &= (uint8_t)~(SK32_CSR0_SENDST | SK32_CSR0_SENTST);
  }
  else {
    /* Clear the stall on the IN endpoint and reset the data toggle.*/
    SK32_USB->CSR &= (uint8_t)~(SK32_INCSR1_SENDST | SK32_INCSR1_SENTST);
    SK32_USB->CSR |= SK32_INCSR1_CLRDT;
  }
  SK32_USB->INDEX = old_index;
}

#endif /* HAL_USE_USB */

/** @} */
