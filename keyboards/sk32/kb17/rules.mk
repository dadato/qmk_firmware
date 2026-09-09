# MCU name: SK32F077 (3Think Cortex-M0, STM32F072-compatible)
MCU = SK32F077

# Bootloader selection
# sk32duino: custom DFU bootloader at 0x08000000 (16 KB), VID:PID 1EAF:0003.
# The QMK application is linked at 0x08004000 by keyboards/sk32/kb17/ld/.
BOOTLOADER = sk32duino

# The SK32F077 has no flash-backed emulated EEPROM configured; use a
# non-persistent transient EEPROM for now.
EEPROM_DRIVER = transient

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
