#---------------------------------------------------------------------------------
# wiiuromm - Wii/vWii Makefile (libogc)
#---------------------------------------------------------------------------------

.SUFFIXES:

TARGET      := wiiuromm
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := include

LIBS        := -lwiiuse -lbte -logc -lm
LIBDIRS     := $(LIBOGC_LIB)

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=$(DEVKITPRO)/devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

# Wii ABI flags from wii_rules.
CFLAGS      := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(MACHDEP)
CXXFLAGS    := $(CFLAGS) -std=gnu++17 -fno-exceptions -fno-rtti
LDFLAGS     := -g $(MACHDEP) -Wl,-Map,$(notdir $*.map) -L$(LIBOGC_LIB)
ASFLAGS     := -g $(MACHDEP)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT    := $(CURDIR)/$(TARGET)
export TOPDIR    := $(CURDIR)
export DEPSDIR   := $(CURDIR)/$(BUILD)

export VPATH     := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                    $(foreach dir,$(DATA),$(CURDIR)/$(dir))

CFILES           := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES         := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES           := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES         := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

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
                     $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                     -I$(CURDIR)/$(BUILD)
export CPPFLAGS   := $(INCLUDE)

export LIBPATHS   := $(foreach dir,$(LIBDIRS),-L$(dir))

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

-include $(DEPENDS)

endif
