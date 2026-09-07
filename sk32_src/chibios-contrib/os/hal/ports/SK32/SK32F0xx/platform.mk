# Required platform files.
# SK32F0xx is register-compatible with STM32F072 except for the USB IP.
PLATFORMSRC := $(CHIBIOS)/os/hal/ports/common/ARMCMx/nvic.c \
               $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx/sk32_isr.c \
               $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx/hal_lld.c \
               $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx/hal_efl_lld.c \
               $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx/hal_st_lld.c \

# Required include directories.
PLATFORMINC := $(CHIBIOS)/os/hal/ports/common/ARMCMx \
               $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx

# Optional platform files.
ifeq ($(USE_SMART_BUILD),yes)

# Configuration files directory
ifeq ($(HALCONFDIR),)
  ifeq ($(CONFDIR),)
    HALCONFDIR = .
  else
    HALCONFDIR := $(CONFDIR)
  endif
endif

HALCONF := $(strip $(shell cat $(HALCONFDIR)/halconf.h | egrep -e "\#define"))

else
endif

# The SK32 PAL (GPIO) driver is native and programs the vendor CMSIS
# registers directly, the shared STM32 GPIOv2 LLD is not used.
ifeq ($(USE_SMART_BUILD),yes)
ifneq ($(findstring HAL_USE_PAL TRUE,$(HALCONF)),)
PLATFORMSRC += $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx/hal_pal_lld.c
endif
else
PLATFORMSRC += $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx/hal_pal_lld.c
endif

# The SK32 USART is a legacy SR/DR class unit, its native serial driver is
# used instead of the shared STM32 USARTv2 LLD (ISR/ICR register set).
ifeq ($(USE_SMART_BUILD),yes)
ifneq ($(findstring HAL_USE_SERIAL TRUE,$(HALCONF)),)
PLATFORMSRC += $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx/hal_serial_lld.c
endif
else
PLATFORMSRC += $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx/hal_serial_lld.c
endif

# The SK32 DMA helper is a native driver managing the shared DMA1 channels
# (CCR/CNDTR/CPAR/CMAR register set of the vendor library).  Its code is
# self-gated on SK32_DMA_REQUIRED, a macro defined by any driver of this
# platform that needs DMA services, so it can be always added to the build
# list like the shared STM32 DMAv1 driver.mk does.
PLATFORMSRC += $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx/sk32_dma.c

# Drivers compatible with the platform.
include $(CHIBIOS)/os/hal/ports/STM32/LLD/ADCv1/driver.mk
include $(CHIBIOS)/os/hal/ports/STM32/LLD/CANv1/driver.mk
include $(CHIBIOS)/os/hal/ports/STM32/LLD/DACv1/driver.mk
include $(CHIBIOS)/os/hal/ports/STM32/LLD/DMAv1/driver.mk
include $(CHIBIOS)/os/hal/ports/STM32/LLD/I2Cv2/driver.mk
include $(CHIBIOS)/os/hal/ports/STM32/LLD/RTCv2/driver.mk
include $(CHIBIOS)/os/hal/ports/STM32/LLD/SPIv2/driver_v2.mk
# The SK32 system tick (ST) driver is native and uses the Cortex-M0 SysTick
# counter in periodic mode, so the shared STM32 SYSTICKv1 LLD is not used.
include $(CHIBIOS)/os/hal/ports/STM32/LLD/TIMv1/driver.mk
# The shared STM32 USARTv2 LLD targets the ISR/ICR register set (STM32F0/F3/L4
# class), the SK32 USART is instead a legacy SR/DR class unit handled by the
# native serial driver added above.
include $(CHIBIOS)/os/hal/ports/STM32/LLD/xWDGv1/driver.mk

# SK32F077 uses a Mentor-MUSB-class USB FS controller (musbfsfc), which is NOT
# compatible with the STM32 USBv1 LLD, so USBv1/driver.mk is not included here.
# The SK32 USB low level driver lives in this port directory instead.
PLATFORMSRC += $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx/hal_usb_lld.c
PLATFORMINC += $(CHIBIOS_CONTRIB)/os/hal/ports/SK32/SK32F0xx

# Shared variables
ALLCSRC += $(PLATFORMSRC)
ALLINC  += $(PLATFORMINC)
