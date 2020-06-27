#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#   If set to $(BUILD), it will statically link in the converted
#   files as if they were data files.
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
APP_TITLE		:= 	Checkpoint
APP_DESCRIPTION	:=	Fast and simple save manager
APP_AUTHOR		:=	Bernardo Giordano, FlagBrew

TARGET		:=	$(subst $e ,_,$(notdir $(APP_TITLE)))
OUTDIR		:=	out
BUILD		:=	build

FORMATSOURCES	:=	source source/dataholders ../common
SOURCES			:=	$(FORMATSOURCES) ../3rd-party/sha256

FORMATINCLUDES	:=	include include/dataholders ../common
INCLUDES		:=	$(FORMATINCLUDES) ../3rd-party/json ../3rd-party/sha256

DATA		:=	data
GRAPHICS	:=	assets/gfx
ROMFS		:=	assets/romfs
GFXBUILD	:=	$(ROMFS)/gfx
SHARKIVE 		:=	../sharkive
CHEATS 			:=	cheats

# If left blank, will try to use "icon.png", "$(TARGET).png", or the default libctru icon, in that order
ICON			:=	assets/icon.png

MAKEROM_	?=	$(shell command -v makerom 2> /dev/null)
BANNERTOOL_	?=	$(shell command -v bannertool 2> /dev/null)

# feel free to comment this out on linux, but on windows I compile the cia with makerom and bannertool in the project folder
CIA_TOOLS_IN_CURDIR := 1

ifneq ($(strip $(CIA_TOOLS_IN_CURDIR)),)

ifeq ($(strip $(MAKEROM)),)
export MAKEROM	:=	$(abspath ./makerom)
endif
ifeq ($(strip $(BANNERTOOL)),)
export BANNERTOOL	:=	$(abspath ./bannertool)
endif

else
export MAKEROM	:= MAKEROM_
export BANNERTOOL	:= BANNERTOOL_
endif

BANNER_AUDIO	=	$(TOPDIR)/assets/audio.wav
BANNER_IMAGE	=	$(TOPDIR)/assets/banner.png

RSF_PATH		=	$(TOPDIR)/assets/app.rsf

# If left blank, makerom will use the default Homebrew logo
LOGO			=	$(TOPDIR)/assets/splash.bin

# If left blank, makerom will use default values (0xff3ff and CTR-P-CTAP, respectively)
UNIQUE_ID		:=	0xBCFFF
PRODUCT_CODE	:=	CTR-HB-CKPT

# Don't really need to change this
ICON_FLAGS		:=	nosavebackups,visible

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -Wextra -Wno-psabi -O3 -mword-relocations -flto \
			-fomit-frame-pointer -ffunction-sections \
			$(ARCH) \
			-DVERSION_MAJOR=${VERSION_MAJOR} \
			-DVERSION_MINOR=${VERSION_MINOR} \
			-DVERSION_MICRO=${VERSION_MICRO} \
			-DGIT_REV=\"${GIT_REV}\" \
			-DCHEAT_SIZE_DECOMPRESSED=${CHEAT_SIZE_DECOMPRESSED}

CFLAGS	+=	$(INCLUDE) -DARM11 -D_3DS -D_GNU_SOURCE=1 -Wno-type-limits

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lbz2 -lcitro2d -lcitro3d -lctru -lm -lstdc++fs

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CTRULIB) $(PORTLIBS)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(OUTDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
FONTFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.ttf)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
ifeq ($(GFXBUILD),$(BUILD))
#---------------------------------------------------------------------------------
export T3XFILES :=  $(GFXFILES:.t3s=.t3x)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
export ROMFS_T3XFILES	:=	$(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
export ROMFS_FONTFILES	:=	$(patsubst %.ttf, $(GFXBUILD)/%.bcfnt, $(FONTFILES))
export T3XHFILES		:=	$(patsubst %.t3s, $(BUILD)/%.h, $(GFXFILES))
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(addsuffix .o,$(T3XFILES))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(GFXFILES:.t3s=.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(OUTPUT).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean format cheats

#---------------------------------------------------------------------------------
all: $(OUTDIR) $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS_T3XFILES) $(ROMFS_FONTFILES) $(T3XHFILES) cheats
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(OUTDIR):
	@mkdir -p $@

$(BUILD):
	@mkdir -p $@

ifneq ($(GFXBUILD),$(BUILD))
$(GFXBUILD):
	@mkdir -p $@
endif

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTDIR) $(GFXBUILD)

#---------------------------------------------------------------------------------
cheats:
	@mkdir -p $(BUILD) $(ROMFS)/$(CHEATS)
ifeq ($(OS),Windows_NT)
	@cd $(SHARKIVE) && py -3 joiner.py 3ds
else
	@cd $(SHARKIVE) && python3 joiner.py 3ds
endif
	@cd $(SHARKIVE)/$(BUILD) && mv 3ds.json.bz2 ../../3ds/$(ROMFS)/$(CHEATS)/$(CHEATS).json.bz2


#---------------------------------------------------------------------------------
format:
	clang-format -i -style=file $(foreach dir,$(FORMATSOURCES),$(wildcard $(dir)/*.c) $(wildcard $(dir)/*.cpp)) $(foreach dir,$(FORMATINCLUDES),$(wildcard $(dir)/*.h) $(wildcard $(dir)/*.hpp))

#---------------------------------------------------------------------------------
$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
$(GFXBUILD)/%.bcfnt :		%.ttf
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@mkbcfnt -o $(GFXBUILD)/$*.bcfnt $<

#---------------------------------------------------------------------------------
export MAKEROM_ARGS		:=	-DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(UNIQUE_ID)" -ver "$$(($(VERSION_MAJOR)*1024+$(VERSION_MINOR)*16+$(VERSION_MICRO)))"

ifneq ($(strip $(LOGO)),)
	export MAKEROM_ARGS	+=	 -logo "$(LOGO)"
endif
ifneq ($(strip $(ROMFS)),)
	export MAKEROM_ARGS	+=	 -DAPP_ROMFS="$(CURDIR)/$(ROMFS)"
endif

ifeq ($(suffix $(BANNER_IMAGE)),.cgfx)
	export BANNER_IMAGE_ARG := -ci
else
	export BANNER_IMAGE_ARG := -i
endif

ifeq ($(suffix $(BANNER_AUDIO)),.cwav)
	export BANNER_AUDIO_ARG := -ca
else
	export BANNER_AUDIO_ARG := -a
endif

#---------------------------------------------------------------------------------
else

.PHONY: all

all: $(OUTPUT).3dsx $(OUTPUT).cia

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OUTPUT).cia	:	$(OUTPUT).elf $(DEPSDIR)/banner.bnr $(DEPSDIR)/icon.icn
	@$(MAKEROM) -f cia -o $@ -target t \
	-elf "$(OUTPUT).elf" \
	-rsf "$(RSF_PATH)" \
	-banner "$(DEPSDIR)/banner.bnr" \
	-icon "$(DEPSDIR)/icon.icn" \
	-exefslogo $(MAKEROM_ARGS)

	@echo "built ... $(notdir $@)"

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

$(DEPSDIR)/banner.bnr:
	@$(BANNERTOOL) makebanner $(BANNER_IMAGE_ARG) "$(BANNER_IMAGE)" $(BANNER_AUDIO_ARG) "$(BANNER_AUDIO)" -o "$@"
	@echo "built ... $(notdir $@)"

$(DEPSDIR)/icon.icn:
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i "$(APP_ICON)" -f "$(ICON_FLAGS)" -o "$@"
	@echo "built ... $(notdir $@)"


#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS	:	%.t3x
#---------------------------------------------------------------------------------
%.t3x.o	%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
# rules for assembling GPU shaders
#---------------------------------------------------------------------------------
define shader-as
	$(eval CURBIN := $*.shbin)
	$(eval DEPSFILE := $(DEPSDIR)/$*.shbin.d)
	echo "$(CURBIN).o: $< $1" > $(DEPSFILE)
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"_end[];" > `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];" >> `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u32" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";" >> `(echo $(CURBIN) | tr . _)`.h
	picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $*.shbin.o
endef

%.shbin.o %_shbin.h : %.v.pica %.g.pica
	@echo $(notdir $^)
	@$(call shader-as,$^)

%.shbin.o %_shbin.h : %.v.pica
	@echo $(notdir $<)
	@$(call shader-as,$<)

%.shbin.o %_shbin.h : %.shlist
	@echo $(notdir $<)
	@$(call shader-as,$(foreach file,$(shell cat $<),$(dir $<)$(file)))

#---------------------------------------------------------------------------------
%.t3x	%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $*.h -d $*.d -o $*.t3x

#---------------------------------------------------------------------------------
%.bcfnt :		%.ttf
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@mkbcfnt -o $*.bcfnt $<

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
