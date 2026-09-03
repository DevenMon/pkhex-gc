# PKHeX-GC Gen III editor/browser - all Gen III
# GameCube homebrew DOL, intended to be launched from Swiss.

.SUFFIXES:

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment (devkitPro/devkitPPC)")
endif

include $(DEVKITPPC)/gamecube_rules

TARGET   := pkhex-gc
BUILD    := build
SOURCES  := source
DATA     := data
INCLUDES := include

CFLAGS   := -g -O2 -Wall -Wextra -Wno-unused-parameter $(MACHDEP) $(INCLUDE)
CXXFLAGS := $(CFLAGS)
LDFLAGS  := -g $(MACHDEP) -Wl,-Map,$(notdir $@).map
LIBS     := -lfat -logc -lm
LIBDIRS  :=

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export VPATH   := $(foreach dir,$(SOURCES) $(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

# Keep the .bin portion in the object name so bin2o also emits the expected
# <filename>_bin symbols (gen3_sprites_bin (and toolchain-dependent size metadata)).
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(sFILES:.s=.o) $(SFILES:.S=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SRC)
export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD) -I$(LIBOGC_INC)
export LIBPATHS := -L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

# Embedded binary data. devkitPro's bin2o creates e.g.
# gen3_sprites_bin (and toolchain-dependent size metadata) from gen3_sprites.bin.
%.bin.o: %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
