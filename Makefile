SUBDIRS = 3ds switch

VERSION_MAJOR	:=	3
VERSION_MINOR	:=	7
VERSION_MICRO	:=	5
GIT_REV="$(shell git rev-parse --short HEAD)"

all: $(SUBDIRS)

clean:
	@for dir in $(SUBDIRS); do $(MAKE) clean -C $$dir; done
	@rm -f sharkive/build/*.json
	@rm -f 3ds/romfs/cheats/*.bz2
	@rm -f switch/romfs/cheats/*.bz2 

3ds: cheats
	@$(MAKE) -C 3ds VERSION_MAJOR=${VERSION_MAJOR} VERSION_MINOR=${VERSION_MINOR} VERSION_MICRO=${VERSION_MICRO} GIT_REV=${GIT_REV} CHEAT_SIZE_DECOMPRESSED=$(shell stat -t "sharkive/build/3ds.json" | awk '{print $$2}')

switch: cheats
	@$(MAKE) -C switch VERSION_MAJOR=${VERSION_MAJOR} VERSION_MINOR=${VERSION_MINOR} VERSION_MICRO=${VERSION_MICRO} GIT_REV=${GIT_REV} CHEAT_SIZE_DECOMPRESSED=$(shell stat -t "sharkive/build/switch.json" | awk '{print $$2}')

format:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir format; done

cppcheck:
	@cppcheck . --enable=all --force 2> cppcheck.log

cheats:
	@make --always-make -C 3ds cheats
	@make --always-make -C switch cheats

.PHONY: $(SUBDIRS) clean format cppcheck cheats