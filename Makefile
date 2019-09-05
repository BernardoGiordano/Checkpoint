SUBDIRS = 3ds switch

VERSION_MAJOR	:=	3
VERSION_MINOR	:=	7
VERSION_MICRO	:=	0
GIT_REV="$(shell git rev-parse --short HEAD)"

all: $(SUBDIRS)

clean:
	@for dir in $(SUBDIRS); do $(MAKE) clean -C $$dir; done

3ds:
	@$(MAKE) -C 3ds cheats
	@$(MAKE) -C 3ds VERSION_MAJOR=${VERSION_MAJOR} VERSION_MINOR=${VERSION_MINOR} VERSION_MICRO=${VERSION_MICRO} GIT_REV=${GIT_REV} CHEAT_SIZE_DECOMPRESSED=$(shell stat -t "sharkive/build/3ds.json" | awk '{print $$2}')

switch:
	@$(MAKE) -C switch cheats
	@$(MAKE) -C switch VERSION_MAJOR=${VERSION_MAJOR} VERSION_MINOR=${VERSION_MINOR} VERSION_MICRO=${VERSION_MICRO} GIT_REV=${GIT_REV} CHEAT_SIZE_DECOMPRESSED=$(shell stat -t "sharkive/build/switch.json" | awk '{print $$2}')

format:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir format; done

cppcheck:
	cppcheck . --enable=all --force 2> cppcheck.log

.PHONY: $(SUBDIRS) clean format cppcheck