# MCU name: SK32F077 (3Think Cortex-M0, STM32F072-compatible)
MCU = SK32F077

# Bootloader selection
# sk32duino: custom DFU bootloader at 0x08000000 (16 KB), VID:PID 1EAF:0003.
# The QMK application is linked at 0x08004000 by keyboards/sk32/ld7_oled/ld/.
BOOTLOADER = sk32duino

# EEPROM persistence: wear-leveling driver backed by the SK32F077 internal
# flash (native EFL / embedded_flash) in the last 2 KB page.
EEPROM_DRIVER = vendor

BOOTMAGIC_ENABLE = yes    # Virtual DIP switch configuration
MOUSEKEY_ENABLE = yes     # Mouse keys
EXTRAKEY_ENABLE = yes     # Audio control and System control
CONSOLE_ENABLE = no       # Console for debug
COMMAND_ENABLE = no       # Commands for debug and configuration
NKRO_ENABLE = yes         # USB Nkey Rollover
BACKLIGHT_ENABLE = no
RGBLIGHT_ENABLE = no

# RGB MATRIX over WS2812, driven by the native SK32F077 SLED peripheral.
RGB_MATRIX_ENABLE = yes
RGB_MATRIX_DRIVER = ws2812
WS2812_DRIVER = sled

# 128x32 SSD1306 OLED on I2C1.
OLED_ENABLE = yes
OLED_DRIVER = ssd1306
OLED_TRANSPORT = i2c

# VIA support (remap + RGB lighting over the HID raw endpoint).  This pulls in
# RAW_ENABLE, DYNAMIC_KEYMAP_ENABLE and TRI_LAYER_ENABLE automatically.
# VIA_INSECURE is only a bring-up convenience so the "secure unlock" never locks
# the board.  Keymap layer/timing hooks in keymaps/default/keymap.c keep
# working alongside the dynamic keymap.
VIA_ENABLE = yes
VIA_INSECURE = yes
