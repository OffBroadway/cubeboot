#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro")
endif

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPRO)/libogc2/gamecube_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	source source/picolibc source/pmalloc
VMC_DIR		:=	$(CURDIR)/../vmc
VMC_CFILES	:=	cbvmc_exiprobe.c cbvmc_flash_hook.c cbvmc_ipl_dev.c cbvmc_ipl_policy.c cbvmc_ipl_probe.c cbvmc_ipl_probe_dev.c cbvmc_loader.c cbvmc_scan.c cbvmc_words.c   # cubeboot_vmc: the patch-side files of the one vmc/ tree (the rest of it is the resident image and host tests)
DATA		:=	data
INCLUDES	:=	include

ifneq ($(BUILD),$(notdir $(CURDIR)))
	export PROJDIR	:=	$(CURDIR)
else
	export PROJDIR	:=	$(dir $(CURDIR))
endif

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------

CFLAGS		= -O2 -Wall -Wno-main -ffixed-r12 -ffixed-r13 -ffunction-sections -fdata-sections -Wno-unused-function -Werror-implicit-function-declaration $(MACHDEP) $(INCLUDE)
CXXFLAGS	= $(CFLAGS)

# G4 stage 1 (docs/virtual_memory_card.md): CBVMC_IPL=1 compiles the read-only IPL install in; without it nothing changes
ifneq ($(strip $(CBVMC_IPL)),)
CFLAGS		+= -DCBVMC_IPL
endif

# G3 diagnostic IPL probe (docs/virtual_memory_card.md): CBVMC_IPL_PROBE=1 in the environment compiles the read-only probe in; without it nothing changes
ifneq ($(strip $(CBVMC_IPL_PROBE)),)
CFLAGS		+= -DCBVMC_IPL_PROBE
endif

LINKER_SCRIPTS	:=
LINKER_SCRIPTS	+= patch.ld
LINKER_SCRIPTS	+= $(PROJDIR)/linker/link.ld
LINKER_SCRIPTS	+= $(PROJDIR)/linker/link_ntsc10.ld
LINKER_SCRIPTS	+= $(PROJDIR)/linker/link_ntsc11.ld
LINKER_SCRIPTS	+= $(PROJDIR)/linker/link_ntsc12_001.ld
LINKER_SCRIPTS	+= $(PROJDIR)/linker/link_ntsc12_101.ld
LINKER_SCRIPTS	+= $(PROJDIR)/linker/link_pal10.ld
LINKER_SCRIPTS	+= $(PROJDIR)/linker/link_pal11.ld
LINKER_SCRIPTS	+= $(PROJDIR)/linker/link_pal12.ld
LINKER_SCRIPTS	+= $(PROJDIR)/linker/combined.ld
LDFLAGS		= $(MACHDEP) -Wl,-Map,$(notdir $@).map $(foreach script,$(LINKER_SCRIPTS),-T $(script)) -Wl,--gc-keep-exported -Wl,--nmagic -nolibc -nostdlib -nodefaultlibs

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
LIBS	:= -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
			$(VMC_DIR)

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
#---------------------------------------------------------------------------------
CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))) $(VMC_CFILES)
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(sFILES:.s=.o) $(SFILES:.S=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES) fake.o

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))

#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			-I$(CURDIR)/.. -I$(VMC_DIR) -I$(CURDIR)/source \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD) \
			-I$(LIBOGC_INC)

#---------------------------------------------------------------------------------
# build a list of library paths
#---------------------------------------------------------------------------------
export LIBPATHS	:=	-L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export OUTPUT	:=	$(CURDIR)/$(TARGET)
.PHONY: $(BUILD) clean

#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean $(TARGET) ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES) : $(HFILES)

# TODO: there has got to be a better way to create an elf dep
fake.o: patch.o
	@$(PROJDIR)/scripts/patch-size.py $^
	@touch $@

#---------------------------------------------------------------------------------
# This rule links in binary data with the .bin extension
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h: %.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
