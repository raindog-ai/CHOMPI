# Project Name
TARGET = CHOMPI

DEBUG=0
# OPT=-O0
#OPT=-Og -g
OPT=-O3

APP_TYPE=BOOT_SRAM
LDSCRIPT = chompi_sram.lds

LDSCRIPT=chompi_sram.lds

# FreeRTOS coreJSON
C_SOURCES = ../libs/coreJSON/source/core_json.c
C_INCLUDES +=  -I../libs/coreJSON/source/include

# Sources
CPP_SOURCES = chompi_main.cpp \
	encoder.cpp \
	FileStreamingManager.cpp

USE_FATFS = 1

# Libs
LIBDAISY_DIR = ../libs/libDaisy
DAISYSP_DIR = ../libs/DaisySP

# Opt-in composite USB build. Ordinary TEMPO/Trad-Seq still uses bundled libDaisy.
ifeq ($(USB_AUDIO),1)
TARGET = CHOMPI_USB_FX_MIDI
C_DEFS += -DCHOMPI_USB_AUDIO
CPP_SOURCES += usb/UsbAudioMidi.cpp
C_SOURCES += $(LIBDAISY_DIR)/src/usbd/usbd_conf.c \
 $(LIBDAISY_DIR)/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c \
 $(LIBDAISY_DIR)/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
endif

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
