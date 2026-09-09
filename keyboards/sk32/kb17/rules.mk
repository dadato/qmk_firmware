# MCU name: SK32F077 (3Think Cortex-M0, STM32F072-compatible)
MCU = SK32F077

# Bootloader selection
# sk32duino: custom DFU bootloader at 0x08000000 (16 KB), VID:PID 1EAF:0003.
# The QMK application is linked at 0x08004000 by keyboards/sk32/kb17/ld/.
BOOTLOADER = sk32duino

# EEPROM persistence: wear-leveling driver backed by the SK32F077 internal
# flash (native EFL / embedded_flash) in the last 2 KB page. The platform
# (common_features.mk) routes MCU_SERIES == SK32F0xx to WEAR_LEVELING_DRIVER ==
# embedded_flash and enables HAL_USE_EFL automatically.
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

# VIA support (remap + config + lighting over the HID raw endpoint).  This
# automatically pulls in RAW_ENABLE, DYNAMIC_KEYMAP_ENABLE, TRI_LAYER_ENABLE and
# (redundantly) BOOTMAGIC_ENABLE.  Persistence goes through the wear-leveling
# EEPROM already enabled above.  VIAL_INSECURE is only a dev convenience so the
# "secure unlock" does not lock the board during bring-up.
VIA_ENABLE = yes
VIA_INSECURE = yes
