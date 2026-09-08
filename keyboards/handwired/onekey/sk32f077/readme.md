# 3Think SK32F077 onekey

* Supported Hardware: 3Think SK32F077 (register-compatible with the STM32F072
  except for the Mentor/MUSB "musbfsfc" USB controller)
* USB HID keyboard: VID 0xFEED / PID 0x6465, one 1x1 DIRECT-pin key

To press the key, short *PB5* to ground (PB5 is an input with an internal
pull-up; low = pressed).

This board validates the SK32F0xx platform integration (SK32F077 MCU,
GENERIC_SK32_F077 board) in QMK: USB enumeration, EP0 / descriptor handling
and the full key chain (matrix -> process_record -> HID report).

The production firmware is a plain onekey.  The platform bring-up scaffolding
(automatic 10x key-press loop driven on PB6, USART1 'alive' heartbeat on
PA0/PA1, USB trace ring) is compiled out by default and can be re-enabled
with:

    qmk compile -kb handwired/onekey/sk32f077 -km default -e SK32_BRINGUP_TESTS=yes
