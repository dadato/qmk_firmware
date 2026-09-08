# The SK32F077 MCU has no dedicated EEPROM and its flash-based emulated EEPROM
# driver is not configured, so use a non-persistent transient EEPROM for now.
EEPROM_DRIVER = transient

# SK32F077 bring-up test scaffolding (auto key-press on PB6/PB5, USART1
# 'alive' heartbeat, USB trace ring).  This is for platform bring-up only and
# is compiled OUT of the production firmware.  Re-enable with:
#   qmk compile -kb handwired/onekey/sk32f077 -km default -e SK32_BRINGUP_TESTS=yes
SK32_BRINGUP_TESTS ?= no
ifeq ($(strip $(SK32_BRINGUP_TESTS)), yes)
    OPT_DEFS += -DSK32_BRINGUP_TESTS
endif