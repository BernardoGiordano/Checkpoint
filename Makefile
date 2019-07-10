SUBDIRS = 3ds switch

VERSION_MAJOR	:=	4
VERSION_MINOR	:=	0
VERSION_MICRO	:=	0
GIT_REV="$(shell git rev-parse --short HEAD)"

all: $(SUBDIRS)

clean:
	@for dir in $(SUBDIRS); do $(MAKE) clean -C $$dir; done

3ds:
	@$(MAKE) -C 3ds VERSION_MAJOR=${VERSION_MAJOR} VERSION_MINOR=${VERSION_MINOR} VERSION_MICRO=${VERSION_MICRO} GIT_REV=${GIT_REV}

switch:
	@$(MAKE) -C switch VERSION_MAJOR=${VERSION_MAJOR} VERSION_MINOR=${VERSION_MINOR} VERSION_MICRO=${VERSION_MICRO} GIT_REV=${GIT_REV}

format:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir format; done

cppcheck:
	cppcheck . --enable=all --force 2> cppcheck.log

.PHONY: $(SUBDIRS) clean format cppcheck