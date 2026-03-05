#---------------------------------------------------------------------------------
# wiiuromm - Wii/vWii Makefile (libogc)
#---------------------------------------------------------------------------------

APP_NAME    := wiiuromm
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := include

LIBS        := -logc -lwiiuse -lbte

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=$(DEVKITPRO)/devkitPPC")
endif

include $(DEVKITPPC)/wii_rules
