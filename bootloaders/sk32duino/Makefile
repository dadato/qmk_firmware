##############################################################################
# Build global options
# NOTE: Can be overridden externally.
#

# Compiler options here.
ifeq ($(USE_OPT),)
  USE_OPT = -Os -ggdb -fomit-frame-pointer
endif

# C specific options here (added to USE_OPT).
ifeq ($(USE_COPT),)
  USE_COPT =
endif

# C++ specific options here (added to USE_OPT).
ifeq ($(USE_CPPOPT),)
  USE_CPPOPT = -fno-rtti
endif

# Enable this if you want the linker to remove unused code and data.
ifeq ($(USE_LINK_GC),)
  USE_LINK_GC = yes
endif

# Linker extra options here.
ifeq ($(USE_LDOPT),)
  USE_LDOPT =
endif

# Enable this if you want link time optimizations (LTO).
# NOTE: left at "no" because -flto + a toolchain path containing spaces
# breaks the link on Windows (lto-wrapper -> make: Permission denied).
ifeq ($(USE_LTO),)
  USE_LTO = no
endif

# Enable this if you want to see the full log while compiling.
ifeq ($(USE_VERBOSE_COMPILE),)
  USE_VERBOSE_COMPILE = no
endif

# If enabled, this option makes the build process faster by not compiling
# modules not used in the current configuration.
ifeq ($(USE_SMART_BUILD),)
  USE_SMART_BUILD = yes
endif

#
# Build global options
##############################################################################

##############################################################################
# Architecture or project specific options
#

# Stack size to be allocated to the Cortex-M process stack. This stack is
# the stack used by the main() thread.
ifeq ($(USE_PROCESS_STACKSIZE),)
  USE_PROCESS_STACKSIZE = 0x400
endif

# Stack size to the allocated to the Cortex-M main/exceptions stack. This
# stack is used for processing interrupts and exceptions.
ifeq ($(USE_EXCEPTIONS_STACKSIZE),)
  USE_EXCEPTIONS_STACKSIZE = 0x400
endif

# Enables the use of FPU (no, softfp, hard).
ifeq ($(USE_FPU),)
  USE_FPU = no
endif

# FPU-related options.
ifeq ($(USE_FPU_OPT),)
  USE_FPU_OPT =
endif

#
# Architecture or project specific options
##############################################################################

##############################################################################
# Project, target, sources and paths
#

# Keyboard this bootloader image is built for.  Selects the DFU boot key:
#   make BOOT_KEY=kb17   (default) - NumLock matrix[0][0] on PB2 x PB12
#   make BOOT_KEY=ld7              - LD7 direct-wire KEY1 on PB12
#   make BOOT_KEY=onekey           - bring-up onekey direct key on PB5
BOOT_KEY ?= kb17

# Define project name here.  The keyboard tag keeps the two builds distinct
# (build/sk32duino_kb17.bin vs build/sk32duino_ld7.bin).
PROJECT = sk32duino_$(BOOT_KEY)

# Target settings.
MCU  = cortex-m0

# Imported source files and paths.
CHIBIOS  := ../../lib/chibios
CHIBIOS_CONTRIB := ../../lib/chibios-contrib
CONFDIR  := ./cfg
BUILDDIR := ./build
DEPDIR   := ./.dep
BOARDDIR := $(CHIBIOS_CONTRIB)/os/hal/boards/GENERIC_SK32_F077

# Licensing files.
include $(CHIBIOS)/os/license/license.mk
# Startup files.
include $(CHIBIOS_CONTRIB)/os/common/startup/ARMCMx/compilers/GCC/mk/startup_SK32F0xx.mk
# HAL-OSAL files (optional).
include $(CHIBIOS)/os/hal/hal.mk
include $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx/platform.mk
include $(BOARDDIR)/board.mk
include $(CHIBIOS)/os/hal/osal/rt-nil/osal.mk
# RTOS files (optional).
include $(CHIBIOS)/os/rt/rt.mk
include $(CHIBIOS)/os/common/ports/ARMv6-M/compilers/GCC/mk/port.mk
# Auto-build files in ./source recursively.
include $(CHIBIOS)/tools/mk/autobuild.mk

# Define linker script file here
LDSCRIPT = ./sk32duino.ld

# C sources that can be compiled in ARM or THUMB mode depending on the global
# setting.
CSRC = $(ALLCSRC)

# C++ sources that can be compiled in ARM or THUMB mode depending on the global
# setting.
CPPSRC = $(ALLCPPSRC)

# List ASM source files here.
ASMSRC = $(ALLASMSRC)

# List ASM with preprocessor source files here.
ASMXSRC = $(ALLXASMSRC)

# Inclusion directories.
INCDIR = $(CONFDIR) $(ALLINC)

# Define C warning options here.
CWARN = -Wall -Wextra -Wundef -Wstrict-prototypes

# Define C++ warning options here.
CPPWARN = -Wall -Wextra -Wundef

#
# Project, target, sources and paths
##############################################################################

##############################################################################
# Start of user section
#

# List all user C define here, like -D_DEBUG=1
# Family-wide macro: all SK32 parts are SK32F077X.  Capacity (64/128 KB) is
# read at runtime from the F_SIZE register, so one boot image fits every part.
UDEFS = -DSK32F077
# Keyboard-specific DFU boot-key selector (kept in sync with BOOT_KEY above).
ifeq ($(BOOT_KEY),ld7)
  UDEFS += -DSK32_DFU_KEY_LD7
else ifeq ($(BOOT_KEY),onekey)
  UDEFS += -DSK32_DFU_KEY_ONEKEY
else
  UDEFS += -DSK32_DFU_KEY_KB17
endif

# Define ASM defines here
UADEFS = -DSK32F077

# List all user directories here
UINCDIR =

# List the user directory to look for the libraries here
ULIBDIR =

# List all user libraries here
ULIBS =

#
# End of user section
##############################################################################

##############################################################################
# Common rules
#

RULESPATH = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk
include $(RULESPATH)/arm-none-eabi.mk
include $(RULESPATH)/rules.mk

#
# Common rules
##############################################################################
