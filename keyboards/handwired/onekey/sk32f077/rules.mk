# The SK32F077 MCU has no dedicated EEPROM and its flash-based emulated EEPROM
# driver is not configured, so use a non-persistent transient EEPROM for now.
EEPROM_DRIVER = transient