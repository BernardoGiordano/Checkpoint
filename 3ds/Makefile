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
APP_DESCRIPTION	:=	Checkpoint: save manager
APP_AUTHOR		:=	Bernardo Giordano, FlagBrew

TARGET			:=	$(subst $e ,_,$(notdir $(APP_TITLE)))
OUTDIR			:=	out
BUILD			:=	build
FORMATSOURCES	:=	source source/script ../common ../common/script
SOURCES			:=	$(FORMATSOURCES) ../3rd-party/sha256 \
					../3rd-party/ftpd/source \
					../common/script/cstdlib \
					../3rd-party/picoc/source/interpreter
DATA			:=	data
FORMATINCLUDES	:=	include ../common ../common/script
INCLUDES		:=	$(FORMATINCLUDES) ../3rd-party/json ../3rd-party/sha256 ../3rd-party/ftpd/include \
					../3rd-party/picoc/include
GRAPHICS		:=	assets/gfx
ROMFS			:=	assets/romfs
GFXBUILD		:=	$(ROMFS)/gfx

# The bundled picoc scripts live in ../scripts: common/ ships on both consoles,
# 3ds/ only here. Both trees are copied into romfs before packing, so
# $(ROMFS)/scripts is generated and gitignored.
SCRIPTSRC		:=	../scripts
ROMFS_SCRIPTS	:=	$(ROMFS)/scripts

MONO_TTF		:=	../switch/romfs/fonts/SpaceMono-Regular.ttf
MONO_BCFNT		:=	$(ROMFS)/fonts/SpaceMono.bcfnt
# 7pt: mkbcfnt turns that into a 14px line feed and a 6px advance, so the pane
# holds ~15 rows of ~62 columns on the 400x240 top screen. A transcript is
# scanned rather than read line by line, so density beats size here.
MONO_SIZE		:=	7

# If left blank, will try to use "icon.png", "$(TARGET).png", or the default ctrulib icon, in that order
ICON			:=	assets/icon.png

BANNER_AUDIO	:=	assets/audio.wav
BANNER_IMAGE	:=	assets/banner.png
RSF_PATH		:=	assets/app.rsf


# If left blank, makerom will use the default Homebrew logo
LOGO			:=	assets/splash.bin

# If left blank, makerom will use default values (0xff3ff and CTR-P-CTAP, respectively)
UNIQUE_ID		:=	0xBCFFF
PRODUCT_CODE	:=	CTR-HB-CKPT

# Don't really need to change this
ICON_FLAGS          :=	nosavebackups,visible

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -gdwarf-4 -Wall -Wextra -Wno-psabi -O3 -mword-relocations -flto=auto \
			-fomit-frame-pointer -ffunction-sections \
			-Wno-unused-parameter \
			$(ARCH) \
			-DVERSION_MAJOR=${VERSION_MAJOR} \
			-DVERSION_MINOR=${VERSION_MINOR} \
			-DVERSION_MICRO=${VERSION_MICRO} \
			-DGIT_REV=\"${GIT_REV}\" \
			-DJSON_HAS_FILESYSTEM=0 \
			-DJSON_HAS_EXPERIMENTAL_FILESYSTEM=0

CFLAGS	+=	$(INCLUDE) -DARM11 -D__3DS__ -D_GNU_SOURCE=1

# picoc: UNIX_HOST is the only host flavour that fits newlib (the alternative is
# MSVC). Its platform.h comes from common/script, not the submodule.
# DEBUGGER doesn't pull in the interactive debugger (debug.c is filtered out of
# CFILES below): it makes ParseStatement call DebugCheckStatement, which
# ../common/script/scriptabort.c implements as the script-abort kill switch.
CFLAGS	+=	-DUNIX_HOST -DDEBUGGER
# picoc's C frames carry no unwind info by default, so a C++ exception thrown in
# a script binding (e.g. std::bad_alloc parsing a large web response) cannot
# unwind back through them and std::terminate()s the process. Emit unwind tables
# so such a throw reaches the catch in ScriptEngine::run instead of aborting.
CFLAGS	+=	-funwind-tables

CXXFLAGS	:= $(CFLAGS) -fno-rtti -std=gnu++23

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lcitro2d -lcitro3d -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru -lm

CCACHE	:=	$(shell command -v ccache 2>/dev/null)
ifneq ($(strip $(CCACHE)),)
CXX		:=	$(CCACHE) $(CXX)
CC		:=	$(CCACHE) $(CC)
endif

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
# picoc's interactive debugger stays out of the build; its Debug* entry points
# come from ../common/script/scriptabort.c (the script-abort hook) instead.
CFILES		:=	$(filter-out debug.c,$(CFILES))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
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

export T3XFILES		:=	$(GFXFILES:.t3s=.t3x)

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(if $(filter $(BUILD),$(GFXBUILD)),$(addsuffix .o,$(T3XFILES)))

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
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(OUTDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all binaries buildelf directories clean format

#---------------------------------------------------------------------------------
# Everything below is expressed as file targets so that a no-op build stays a
# no-op: bannertool, makerom and the romfs staging only run when one of their
# inputs actually moved. The recursive make into $(BUILD) is the exception, it
# runs every time to do its own dependency check, but it leaves the elf
# untouched when there is nothing to relink, so the cia isn't repacked either.
#---------------------------------------------------------------------------------
ROMFS_SPRITES	:=	$(GFXBUILD)/sprites.t3x
# Everything that ends up inside the romfs image: the trees generated below,
# plus the files checked into $(ROMFS) as they are (config.json, i18n.json, ...).
ROMFS_STATIC	:=	$(shell find $(ROMFS) -type f -not -path "$(GFXBUILD)/*" -not -path "$(ROMFS_SCRIPTS)/*" -not -path "$(dir $(MONO_BCFNT))*")
export ROMFS_DEPS	:=	$(ROMFS_SPRITES) $(MONO_BCFNT) $(ROMFS_SCRIPTS) $(ROMFS_STATIC)

# bannertool and makerom read the app metadata (title, version, ids) straight
# out of these makefiles, so editing one has to invalidate their outputs.
MAKEFILE_DEPS	:=	$(CURDIR)/Makefile $(CURDIR)/../Makefile

#---------------------------------------------------------------------------------
all: binaries

binaries: $(OUTPUT).3dsx $(OUTPUT).cia

# Those checked-in files are inputs, never targets. The empty recipe stops make
# from matching them against an implicit rule: %.smdh would happily regenerate
# assets/romfs/PKSM.smdh from Checkpoint's own icon and title.
$(ROMFS_STATIC): ;
#---------------------------------------------------------------------------------
directories:
	@mkdir -p $(BUILD) $(GFXBUILD) $(OUTDIR)
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS) $(ROMFS_DEPS) | directories
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile $(OUTPUT).3dsx
#---------------------------------------------------------------------------------
$(OUTPUT).cia: $(OUTPUT).elf $(BUILD)/banner.bnr $(BUILD)/icon.icn $(LOGO) $(RSF_PATH) $(ROMFS_DEPS) $(MAKEFILE_DEPS) | directories
	@makerom -f cia -o $(OUTPUT).cia -target t -exefslogo -elf "$(OUTPUT).elf" -rsf "$(RSF_PATH)" -ver "$$(($(VERSION_MAJOR)*1024+$(VERSION_MINOR)*16+$(VERSION_MICRO)))" -banner "$(BUILD)/banner.bnr" -icon "$(BUILD)/icon.icn" -DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(UNIQUE_ID)" -logo "$(LOGO)"
	@echo built ... $(notdir $@)
#---------------------------------------------------------------------------------
# The elf is linked by the recursive make below; this pair lets the outer make
# see its timestamp, so the 3dsx and the cia only rerun when it really changed.
$(OUTPUT).elf: buildelf | directories
	@true

buildelf: $(ROMFS_SPRITES) | directories
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile buildelf
#---------------------------------------------------------------------------------
$(BUILD)/banner.bnr: $(BANNER_IMAGE) $(BANNER_AUDIO) | directories
	@bannertool makebanner -i "$(BANNER_IMAGE)" -a "$(BANNER_AUDIO)" -o $@
#---------------------------------------------------------------------------------
$(BUILD)/icon.icn: $(APP_ICON) $(MAKEFILE_DEPS) | directories
	@bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i "$(APP_ICON)" -f "$(ICON_FLAGS)" -o $@
#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(GFXBUILD) $(OUTDIR) $(MONO_BCFNT) $(ROMFS_SCRIPTS)
#---------------------------------------------------------------------------------
$(MONO_BCFNT): $(MONO_TTF) $(CURDIR)/Makefile
	@mkdir -p $(dir $@)
	@mkbcfnt -s $(MONO_SIZE) -o $@ $<
#---------------------------------------------------------------------------------
# The directory's own timestamp is the stamp: deleting it re-stages the tree.
$(ROMFS_SCRIPTS): $(shell find $(SCRIPTSRC)/common $(SCRIPTSRC)/3ds -type f)
	@rm -fr $@
	@mkdir -p $@
	@cp -R $(SCRIPTSRC)/common/. $@
	@cp -R $(SCRIPTSRC)/3ds/. $@
	@touch $@
#---------------------------------------------------------------------------------
$(ROMFS_SPRITES): $(wildcard $(GRAPHICS)/*.t3s) $(wildcard $(GRAPHICS)/*.png) | directories
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile sprites.t3x
	@cp $(BUILD)/sprites.t3x $@
#---------------------------------------------------------------------------------
format:
	clang-format -i -style=file $(foreach dir,$(FORMATSOURCES),$(wildcard $(dir)/*.c) $(wildcard $(dir)/*.cpp) $(wildcard $(dir)/*.tcc)) $(foreach dir,$(FORMATINCLUDES),$(wildcard $(dir)/*.h) $(wildcard $(dir)/*.hpp))
#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
# 3dsxtool packs $(ROMFS) into the 3dsx, so the romfs trees are inputs here too:
# without them the outer make would keep re-entering this one for a repack that
# never happens.
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS) $(ROMFS_DEPS:%=../%)

# Same guard as above, plus it keeps this make from trying to regenerate the
# romfs trees the outer one owns.
$(ROMFS_DEPS:%=../%): ;

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

# Asked for by name so that make stays quiet when the elf is already up to date:
# a phony goal has nothing to report, an up to date file target does.
.PHONY: buildelf
buildelf: $(OUTPUT).elf
	@:

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
%.t3x.o	%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
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

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
