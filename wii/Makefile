#---------------------------------------------------------------------------------
# wiiuromm - Wii/vWii Makefile (libogc)
#---------------------------------------------------------------------------------

.SUFFIXES:

TARGET      := wiiuromm
BUILD       := build
SOURCES     := source ../core
DATA        := data data/sgm ../external/libwiigui/source/fonts ../external/libwiigui/source/lang ../external/libwiigui/source/images ../external/libwiigui/source/sounds
INCLUDES    := include ../include ../external/libwiigui/source ../external/libwiigui/source/libwiigui
EXTRA_VPATH := ../external/libwiigui/source ../external/libwiigui/source/libwiigui
EXTERNAL_CPPFILES := \
	../external/libwiigui/source/FreeTypeGX.cpp \
	../external/libwiigui/source/gettext.cpp \
	../external/libwiigui/source/libwiigui/gui_element.cpp \
	../external/libwiigui/source/libwiigui/gui_button.cpp \
	../external/libwiigui/source/libwiigui/gui_sound.cpp \
	../external/libwiigui/source/libwiigui/gui_trigger.cpp \
	../external/libwiigui/source/libwiigui/gui_text.cpp \
	../external/libwiigui/source/libwiigui/gui_imagedata.cpp \
	../external/libwiigui/source/libwiigui/gui_image.cpp \
	../external/libwiigui/source/libwiigui/gui_window.cpp \
	../external/libwiigui/source/libwiigui/gui_filebrowser.cpp
EXTERNAL_CFILES := \
	../external/libwiigui/source/pngu.c

LIBS        := -lwiiuse -lbte -lasnd -logc -lfreetype -lpng -lbz2 -lz -lm
LIBDIRS     := $(LIBOGC_LIB)

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=$(DEVKITPRO)/devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

# Wii ABI flags from wii_rules.
CFLAGS      := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(MACHDEP) -DWIIUROMM_MIN_FILELIST=1 -DNO_SOUND
CXXFLAGS    := $(CFLAGS) -std=gnu++17 -fno-rtti
LDFLAGS     := -g $(MACHDEP) -Wl,-Map,$(notdir $*.map) -L$(LIBOGC_LIB)
ASFLAGS     := -g $(MACHDEP)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT    := $(CURDIR)/$(TARGET)
export TOPDIR    := $(CURDIR)
export DEPSDIR   := $(CURDIR)/$(BUILD)

export VPATH     := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                    $(foreach dir,$(EXTRA_VPATH),$(CURDIR)/$(dir)) \
                    $(foreach dir,$(DATA),$(CURDIR)/$(dir))

CFILES           := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CFILES           += $(notdir $(EXTERNAL_CFILES))
CPPFILES         := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
CPPFILES         += $(notdir $(EXTERNAL_CPPFILES))
SFILES           := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES         := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.ttf) \
                    $(wildcard $(dir)/*.lang) $(wildcard $(dir)/*.png) \
                    $(wildcard $(dir)/*.ogg) $(wildcard $(dir)/*.pcm)))

ifeq ($(strip $(CPPFILES)),)
export LD        := $(CC)
else
export LD        := $(CXX)
endif

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE    := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                     -I$(LIBOGC_INC) \
                     $(foreach dir,$(PORTLIBS),-I$(dir)/include) \
                     $(foreach dir,$(PORTLIBS),-I$(dir)/include/freetype2) \
                     -I$(CURDIR)/$(BUILD)
export CPPFLAGS   := $(INCLUDE)

export LIBPATHS   := $(foreach dir,$(LIBDIRS),-L$(dir)) \
                     $(foreach dir,$(PORTLIBS),-L$(dir)/lib)

.PHONY: all clean $(BUILD)

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).dol

else

.PHONY: all
DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).dol

$(OUTPUT).elf : $(OFILES)
$(OFILES_SRC) : $(HFILES_BIN)

%.ttf.o %_ttf.h : %.ttf
	@echo $(notdir $<)
	$(bin2o)

%.lang.o %_lang.h : %.lang
	@echo $(notdir $<)
	$(bin2o)

%.png.o %_png.h : %.png
	@echo $(notdir $<)
	$(bin2o)

%.ogg.o %_ogg.h : %.ogg
	@echo $(notdir $<)
	$(bin2o)

%.pcm.o %_pcm.h : %.pcm
	@echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)

endif
