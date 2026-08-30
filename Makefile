SUBDIRS = 3ds switch

VERSION_MAJOR	:=	5
VERSION_MINOR	:=	3
VERSION_MICRO	:=	0
GIT_REV	:=	$(shell git rev-parse --short HEAD 2>/dev/null)
ifeq ($(strip $(GIT_REV)),)
GIT_REV	:=	unknown
endif

all: $(SUBDIRS)

clean:
	@for dir in $(SUBDIRS); do $(MAKE) clean -C $$dir; done
	@$(MAKE) clean -C tests

3ds:
	@$(MAKE) -C 3ds VERSION_MAJOR=${VERSION_MAJOR} VERSION_MINOR=${VERSION_MINOR} VERSION_MICRO=${VERSION_MICRO} GIT_REV=${GIT_REV}

switch:
	@$(MAKE) -C switch VERSION_MAJOR=${VERSION_MAJOR} VERSION_MINOR=${VERSION_MINOR} VERSION_MICRO=${VERSION_MICRO} GIT_REV=${GIT_REV}

cli:
	@$(MAKE) -C tools/chlink

format:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir format; done

cppcheck:
	@cppcheck . --enable=all --force 2> cppcheck.log

# Host-side tests. They run on the build machine, not on hardware. See
# tests/Makefile for what each one covers.
test:
	@$(MAKE) -C tests

.PHONY: $(SUBDIRS) cli clean format cppcheck test
