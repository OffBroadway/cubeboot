#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/gamecube_rules

export CC := powerpc-eabi-clang
MACHDEP =  -DGEKKO -mcpu=750 \
	   -D__gamecube__ -DHW_DOL -ffunction-sections -fdata-sections

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	source/ source/fatfs/
DATA		:=	data  
INCLUDES	:=	-nostdlibinc -isystem $(DEVKITPPC)/powerpc-eabi/include

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------

CFLAGS	= -g -Os -Wall $(MACHDEP) $(INCLUDE)
CXXFLAGS	=	$(CFLAGS)

LDFLAGS	=	-g $(MACHDEP) -Wl,-Map,$(notdir $@).map -T$(PWD)/ipl.ld

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
LIBS	:=	-logc

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
export OUTPUT_SX :=	$(CURDIR)/qoob_sx_$(TARGET)_upgrade

export VPATH	:=	$(foreach dir,$(dir $(SOURCES)),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
#---------------------------------------------------------------------------------
CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CFILES		+=	$(foreach file,$(filter %.c,$(SOURCES)),$(notdir $(file)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC) -Wl,--gc-sections -nostartfiles \
		$(DEVKITPPC)/lib/gcc/powerpc-eabi/*/crtend.o \
		$(DEVKITPPC)/lib/gcc/powerpc-eabi/*/ecrtn.o \
		$(DEVKITPPC)/lib/gcc/powerpc-eabi/*/ecrti.o \
		$(DEVKITPPC)/lib/gcc/powerpc-eabi/*/crtbegin.o \
		$(DEVKITPPC)/powerpc-eabi/lib/crtmain.o
else
	export LD	:=	$(CXX)
endif

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
					$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
					$(sFILES:.s=.o) $(SFILES:.S=.o)

#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(LIBOGC_INC)

#---------------------------------------------------------------------------------
# build a list of library paths
#---------------------------------------------------------------------------------
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
					-L$(LIBOGC_LIB)

export OUTPUT	:=	$(CURDIR)/$(TARGET)
.PHONY: $(BUILD) clean

#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).{elf,dol} $(OUTPUT).{gcb,vgc} \
		$(OUTPUT)_xz.{dol,elf,qbsx} $(OUTPUT_SX).{dol,elf} \
		$(OUTPUT)_xeno.{bin,elf}

#---------------------------------------------------------------------------------
run: $(BUILD)
	usb-load $(OUTPUT)_xz.dol


#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all: $(OUTPUT).gcb $(OUTPUT_SX).dol $(OUTPUT).vgc $(OUTPUT)_xeno.bin

$(OUTPUT).elf: $(OFILES)

%.gcb: %.dol
	@echo pack IPL ... $(notdir $@)
	@cd $(PWD); ./dol2ipl.py ipl.rom $< $@

$(OUTPUT)_xz.dol: $(OUTPUT).dol
	@echo compress ... $(notdir $@)
	@dolxz $< $@ -cube

$(OUTPUT)_xz.elf: $(OUTPUT)_xz.dol
	@echo dol2elf ... $(notdir $@)
	@doltool -e $<

%.qbsx: %.elf
	@echo pack IPL ... $(notdir $@)
	@cd $(PWD); ./dol2ipl.py /dev/null $< $@

$(OUTPUT_SX).elf: $(OUTPUT)_xz.qbsx
	@echo splice ... $@
	@cd $(PWD); cp -f qoob_sx_13c_upgrade.elf $@
	@cd $(PWD); dd if=$< of=$@ obs=4 seek=1851 conv=notrunc

%.vgc: %.dol
	@echo pack IPL ... $(notdir $@)
	@cd $(PWD); ./dol2ipl.py /dev/null $< $@

%_xeno.elf: $(OFILES)
	@echo linking ... $(notdir $@)
	@$(LD)  $^ $(LDFLAGS) -Wl,--section-start,.init=0x81700000 $(LIBPATHS) $(LIBS) -o $@

%.bin: %.elf
	@echo extract binary ... $(notdir $@)
	@$(OBJCOPY) -O binary $< $@

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
