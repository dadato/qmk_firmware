/*
    sk32duino - SK32F077 USB DFU bootloader
    Copyright (C) 2026 NUTWANG/QMK

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
 * @file    usbdfu.c
 * @brief   stm32duino compatible USB DFU class implementation.
 *
 * The device enumerates as VID:PID 1EAF:0003 with a single DFU interface
 * (class 0xFE, subclass 0x01, protocol 0x02) exposed through alternate
 * settings 0..2.  dfu-util is invoked with "-a 2" (as with the reference
 * STM32duino bootloader) which selects alternate setting 2; all the media
 * share the same flash backend so the chosen alternate setting only matters
 * to the host tool.
 *
 * Everything runs on endpoint 0: DNLOAD data is received through the EP0
 * OUT phase into @p fw_buffer, the actual flash programming happens in the
 * GETSTATUS completion callback and the DFU functional descriptor carries
 * the transfer size.
 */

#include <string.h>

#include "hal.h"

#include "usbdfu.h"
#include "dfu_target.h"

/* Receive buffer for one DNLOAD chunk (advertised as wTransferSize). */
uint8_t fw_buffer[FW_BUFFER_SIZE];

/*
 * USB Device Descriptor.
 */
static const uint8_t dfu_device_descriptor_data[USB_DESC_DEVICE_SIZE] = {
  USB_DESC_DEVICE(0x0200,         /* bcdUSB (2.0).                          */
                  0x00,           /* bDeviceClass.                          */
                  0x00,           /* bDeviceSubClass.                       */
                  0x00,           /* bDeviceProtocol.                       */
                  64,             /* bMaxPacketSize0.                       */
                  0x1EAF,         /* idVendor (LeafLabs/STM32duino).        */
                  0x0003,         /* idProduct.                             */
                  0x0200,         /* bcdDevice.                             */
                  1,              /* iManufacturer.                         */
                  2,              /* iProduct.                              */
                  0,              /* iSerialNumber.                         */
                  1)              /* bNumConfigurations.                    */
};

static const USBDescriptor dfu_device_descriptor = {
  sizeof dfu_device_descriptor_data,
  dfu_device_descriptor_data
};

/* wTotalLength for the configuration below. */
#define DFU_CONFIG_TOTAL_LENGTH   63U

/* One DFU functional descriptor block: 9 + 9 = 18 bytes per alt setting. */
#define DFU_IFACE_BLOCK(alt)                                                  \
  USB_DESC_INTERFACE(0, (alt), 0, 0xFE, 0x01, 0x02, 0),                      \
  USB_DESC_BYTE(9),                    /* bLength.                          */\
  USB_DESC_BYTE(0x21),                 /* bDescriptorType (DFU_FUNCTIONAL).*/\
  USB_DESC_BYTE(0x0B),                 /* bmAttributes (DETACH|DNLOAD|      */\
                                       /*   UPLOAD).                        */\
  USB_DESC_WORD(1000),                 /* wDetachTimeout (ms).              */\
  USB_DESC_WORD(FW_BUFFER_SIZE),       /* wTransferSize.                    */\
  USB_DESC_BCD(0x0110)                 /* bcdDFU (1.10).                    */

/*
 * Configuration Descriptor tree for a DFU device with alternate settings
 * 0..2.  The alternate settings are the "media" selector used by the host
 * tools (dfu-util -a 2), the device backend always targets the application
 * flash area.
 */
static const uint8_t dfu_configuration_descriptor_data[DFU_CONFIG_TOTAL_LENGTH] = {
  /* Configuration Descriptor. */
  USB_DESC_CONFIGURATION(DFU_CONFIG_TOTAL_LENGTH,  /* wTotalLength.         */
                         1,                         /* bNumInterfaces.       */
                         1,                         /* bConfigurationValue.  */
                         0,                         /* iConfiguration.       */
                         0x80,                      /* bmAttributes (bus     */
                                                    /*   powered).           */
                         100),                      /* bMaxPower (200mA).    */
  /* Interface, alternate setting 0 + DFU functional descriptor. */
  DFU_IFACE_BLOCK(0),
  /* Interface, alternate setting 1 + DFU functional descriptor. */
  DFU_IFACE_BLOCK(1),
  /* Interface, alternate setting 2 + DFU functional descriptor. */
  DFU_IFACE_BLOCK(2)
};

static const USBDescriptor dfu_configuration_descriptor = {
  sizeof dfu_configuration_descriptor_data,
  dfu_configuration_descriptor_data
};

/* String descriptor 0: supported languages. */
static const uint8_t dfu_string0[] = {
  USB_DESC_BYTE(4),                     /* bLength.                         */
  USB_DESC_BYTE(USB_DESCRIPTOR_STRING), /* bDescriptorType.                 */
  USB_DESC_WORD(0x0409)                 /* wLANGID (U.S. English).          */
};

/* String descriptor 1: manufacturer. */
static const uint8_t dfu_string1[] = {
  USB_DESC_BYTE(2 + 3 * 2),             /* bLength ("QMK").                 */
  USB_DESC_BYTE(USB_DESCRIPTOR_STRING), /* bDescriptorType.                 */
  'Q', 0, 'M', 0, 'K', 0
};

/* String descriptor 2: product. */
static const uint8_t dfu_string2[] = {
  USB_DESC_BYTE(2 + 9 * 2),             /* bLength ("sk32duino").           */
  USB_DESC_BYTE(USB_DESCRIPTOR_STRING), /* bDescriptorType.                 */
  's', 0, 'k', 0, '3', 0, '2', 0, 'd', 0, 'u', 0, 'i', 0, 'n', 0, 'o', 0
};

static const USBDescriptor dfu_strings[] = {
  {sizeof dfu_string0, dfu_string0},
  {sizeof dfu_string1, dfu_string1},
  {sizeof dfu_string2, dfu_string2}
};

/*
 * Handles the GET_DESCRIPTOR callback. All required descriptors are
 * handled here.
 */
static const USBDescriptor *get_descriptor(USBDriver *usbp,
                                           uint8_t dtype,
                                           uint8_t dindex,
                                           uint16_t lang) {
  (void)usbp;
  (void)lang;

  switch (dtype) {
  case USB_DESCRIPTOR_DEVICE:
    return &dfu_device_descriptor;
  case USB_DESCRIPTOR_CONFIGURATION:
    return &dfu_configuration_descriptor;
  case USB_DESCRIPTOR_STRING:
    if (dindex < 3) {
      return &dfu_strings[dindex];
    }
    break;
  default:
    break;
  }
  return NULL;
}

volatile enum dfu_state  currentState = STATE_DFU_IDLE;
volatile enum dfu_status currentStatus = DFU_STATUS_OK;
size_t current_dfu_offset = 0;
size_t dfu_download_size = 0;

/* Set when a finished download reaches the end of its manifestation phase:
 * the DFU idle loop resets into the freshly programmed application shortly
 * afterwards (see dfu_boot() in main.c). */
volatile bool usbdfu_app_boot_pending = false;

/* Alternate setting currently selected by the host (GET_INTERFACE). */
static uint8_t dfu_alt_setting = 0;

/*
 * Programs one received chunk into flash.
 * @details Executed synchronously from the DFU_GETSTATUS request handling,
 *          directly in the EP0 interrupt.  No end-of-transfer callback is
 *          involved: on this controller the host status ZLP of a control
 *          read is swallowed by the USB core when the last IN data packet
 *          was armed with DATEND (see the SK32 LLD), so the completion
 *          callbacks of EP0 IN data transfers are never invoked.
 */
static void dfu_program_chunk(void) {
  uint8_t *dest = (uint8_t *)(SK32_APP_BASE + current_dfu_offset);
  bool     ok   = true;

  target_flash_unlock();
  if (current_dfu_offset == 0U) {
    ok = target_prepare_flash();
  }
  if (ok) {
    ok = target_flash_write(dest, fw_buffer, dfu_download_size);
  }
  target_flash_lock();

  if (ok) {
    current_dfu_offset += dfu_download_size;
    currentState = STATE_DFU_DNLOAD_IDLE;
  } else {
    currentState = STATE_DFU_ERROR;
    currentStatus = DFU_STATUS_ERR_WRITE;
  }
}

/*
 * Manifest phase: flush the last partial page of the new firmware and
 * report to the host.  Also synchronous (see dfu_program_chunk()).
 */
static void dfu_manifest(void) {
  bool ok;

  target_flash_unlock();
  ok = target_complete_programming();
  target_flash_lock();

  if (ok) {
    currentState = STATE_DFU_MANIFEST;
  } else {
    currentState = STATE_DFU_ERROR;
    currentStatus = DFU_STATUS_ERR_WRITE;
  }
}

/* DFU_DETACH (dfu-util -R): leave DFU through the same path as a finished
 * download: the idle loop in dfu_boot() first drops the USB bus so the
 * application re-enumerates cleanly.  This callback runs in interrupt
 * context, so only the pending flag is armed here. */
static void dfu_on_detach_complete(USBDriver *usbp) {
  (void)usbp;
  usbdfu_app_boot_pending = true;
}

static inline void dfu_status_req(USBDriver *usbp) {
  static uint8_t status_response_buffer[6];
  uint32_t pollTime = 0;

  switch (currentState) {
  case STATE_DFU_DNLOAD_SYNC: {
    /* A chunk has been received in fw_buffer: program it now so that the
       response already carries the resulting dnLOAD-IDLE state and the
       host never has to poll a busy condition. */
    dfu_program_chunk();
    break;
  }
  case STATE_DFU_MANIFEST_SYNC: {
    /* The zero length DNLOAD has been received: finalize the download by
       flushing the last partially filled page, then report dfuMANIFEST
       and arm the reset into the freshly programmed application. */
    dfu_manifest();
    if (currentState == STATE_DFU_MANIFEST) {
      usbdfu_app_boot_pending = true;
    }
    break;
  }
  case STATE_DFU_MANIFEST: {
    /* dfu-util polls again while the device is in dfuMANIFEST.  Answer
       with dfuMANIFEST-WAIT-RESET: the host stops polling and the pending
       reset boots the new application. */
    currentState = STATE_DFU_MANIFEST_WAIT_RESET;
    break;
  }
  default:
    break;
  }

  /* Response construction. */
  status_response_buffer[0] = (uint8_t)currentStatus;
  status_response_buffer[1] = (uint8_t)(pollTime & 0xFFU);
  status_response_buffer[2] = (uint8_t)((pollTime >> 8U) & 0xFFU);
  status_response_buffer[3] = (uint8_t)((pollTime >> 16U) & 0xFFU);
  status_response_buffer[4] = (uint8_t)currentState;
  status_response_buffer[5] = 0; /* No iString. */

  usbSetupTransfer(usbp, status_response_buffer, 6, NULL);
}

/*
 * USB class request hook. Invoked by the EP0 setup handler before the
 * generic standard request handler; returns true when handled.
 */
static bool request_handler(USBDriver *usbp) {
  struct usb_setup *setup = (struct usb_setup *)usbp->setup;

  if ((setup->bmRequestType & USB_RTYPE_TYPE_MASK) == USB_RTYPE_TYPE_CLASS) {
    switch (setup->bRequest) {
    case DFU_GETSTATUS: {
      dfu_status_req(usbp);
      return true;
    }
    case DFU_GETSTATE: {
      usbSetupTransfer(usbp, (uint8_t *)&currentState, 1, NULL);
      return true;
    }
    case DFU_UPLOAD: {
      switch (currentState) {
      case STATE_DFU_IDLE: {
        current_dfu_offset = 0;
        __attribute__((fallthrough));
      }
      case STATE_DFU_UPLOAD_IDLE: {
        uint16_t copy_len = setup->wLength;
        size_t fw_size = target_get_max_fw_size();

        if (current_dfu_offset + setup->wLength > fw_size) {
          copy_len = (uint16_t)(fw_size - current_dfu_offset);
          currentState = STATE_DFU_IDLE;
        } else {
          currentState = STATE_DFU_UPLOAD_IDLE;
        }
        usbSetupTransfer(usbp,
                         (uint8_t *)(SK32_APP_BASE + current_dfu_offset),
                         copy_len, NULL);
        current_dfu_offset += copy_len;
        break;
      }
      default:
        usbSetupTransfer(usbp, NULL, 0, NULL);
        break;
      }
      return true;
    }
    case DFU_CLRSTATUS: {
      currentStatus = DFU_STATUS_OK;
      currentState = STATE_DFU_IDLE;
      usbSetupTransfer(usbp, NULL, 0, NULL);
      return true;
    }
    case DFU_ABORT: {
      currentState = STATE_DFU_IDLE;
      usbSetupTransfer(usbp, NULL, 0, NULL);
      return true;
    }
    case DFU_DETACH: {
      usbSetupTransfer(usbp, NULL, 0, &dfu_on_detach_complete);
      return true;
    }
    case DFU_DNLOAD: {
      switch (currentState) {
      case STATE_DFU_IDLE: {
        if (setup->wLength == 0U) {
          /* Nothing to download; leave the state machine idle. */
          usbSetupTransfer(usbp, NULL, 0, NULL);
          break;
        }
        current_dfu_offset = 0;
        dfu_download_size = setup->wLength;
        *SK32_DFU_LATCH_ADDR |= SK32_DFU_LATCH_TOUCHED;
        currentState = STATE_DFU_DNLOAD_SYNC;
        usbSetupTransfer(usbp, &fw_buffer[0], dfu_download_size, NULL);
        break;
      }
      case STATE_DFU_DNLOAD_IDLE: {
        if (setup->wLength > 0U) {
          dfu_download_size = setup->wLength;
          *SK32_DFU_LATCH_ADDR |= SK32_DFU_LATCH_TOUCHED;
          usbSetupTransfer(usbp, &fw_buffer[0], dfu_download_size, NULL);
          currentState = STATE_DFU_DNLOAD_SYNC;
        } else {
          /* Zero length DNLOAD: end of the download.  The actual
             manifestation (last page flush) happens on the next
             DFU_GETSTATUS. */
          currentState = STATE_DFU_MANIFEST_SYNC;
          usbSetupTransfer(usbp, NULL, 0, NULL);
        }
        break;
      }
      default: {
        usbSetupTransfer(usbp, NULL, 0, NULL);
        break;
      }
      }
      return true;
    }
    default:
      break;
    }
  }

  /* The ChibiOS USB core deliberately does not implement SET_INTERFACE /
   * GET_INTERFACE (see hal_usb.c).  dfu-util selects the target media with
   * SET_INTERFACE, so acknowledge it here and reset the DFU state machine.
   */
  if ((setup->bmRequestType & USB_RTYPE_TYPE_MASK) == USB_RTYPE_TYPE_STD) {
    if ((setup->bmRequestType & USB_RTYPE_RECIPIENT_MASK) ==
        USB_RTYPE_RECIPIENT_INTERFACE) {
      switch (setup->bRequest) {
      case USB_REQ_SET_INTERFACE:
        dfu_alt_setting = (uint8_t)(setup->wValue & 0xFFU);
        currentStatus = DFU_STATUS_OK;
        currentState = STATE_DFU_IDLE;
        current_dfu_offset = 0U;
        dfu_download_size = 0U;
        usbSetupTransfer(usbp, NULL, 0, NULL);
        return true;
      case USB_REQ_GET_INTERFACE:
        usbSetupTransfer(usbp, &dfu_alt_setting, 1, NULL);
        return true;
      default:
        break;
      }
    }
  }
  return false;
}

/*
 * USB driver configuration.
 */
const USBConfig usbcfg = {
  NULL,               /* event_cb.          */
  get_descriptor,     /* get_descriptor_cb. */
  request_handler,    /* requests_hook_cb.  */
  NULL                /* sof_cb.            */
};
