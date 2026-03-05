#---------------------------------------------------------------------------------
# wiiuromm - native Wii U Makefile (wut)
#---------------------------------------------------------------------------------

.SUFFIXES:

TARGET      := wiiuromm
BUILD       := build
SOURCES     := source core
DATA        := data
INCLUDES    := include

export APP_NAME      := wiiuromm
export APP_SHORTNAME := wiiuromm
export APP_AUTHOR    := shalasere
export APP_VERSION   := 0.1.0

CFLAGS      := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections
CXXFLAGS    := $(CFLAGS) -std=gnu++17 -fno-exceptions -fno-rtti
LDFLAGS     := -Wl,-Map,$(notdir $*.map),--gc-sections
LIBS        := -lwut
LIBDIRS     :=

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=$(DEVKITPRO)/devkitPPC")
endif

export WUT_ROOT ?= $(DEVKITPRO)/wut
LIBDIRS     := $(WUT_ROOT)
WUT_RULES := $(WUT_ROOT)/share/wut_rules
ifeq ($(wildcard $(WUT_RULES)),)
$(error "wut_rules not found at $(WUT_RULES). Install the wut package.")
endif

include $(WUT_RULES)

# Wii U ABI/stdlib settings provided by wut rules.
CFLAGS      += $(MACHDEP)
CXXFLAGS    += $(MACHDEP)
LDFLAGS     += $(RPXSPECS) $(MACHDEP)

export WUT_MAKE_RPX := 1

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
                     -I$(WUT_ROOT)/include \
                     $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                     -I$(CURDIR)/$(BUILD)
export CPPFLAGS   := $(INCLUDE)

export LIBPATHS   := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean runtime runtime-visible validate validate-visible $(BUILD)

all: $(BUILD)

runtime:
	@$(MAKE) --no-print-directory -C tests runtime

runtime-visible:
	@$(MAKE) --no-print-directory -C tests runtime-visible

validate:
	@$(MAKE) --no-print-directory -C tests test
	@$(MAKE) --no-print-directory runtime

validate-visible:
	@$(MAKE) --no-print-directory -C tests test
	@$(MAKE) --no-print-directory runtime-visible

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).rpx $(TARGET).wuhb

else

.PHONY: all
DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).rpx

$(OUTPUT).elf : $(OFILES)
$(OFILES_SRC) : $(HFILES_BIN)

-include $(DEPENDS)

endif
