SUBDIRS = 3ds switch

all:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir; done

clean:
	@for dir in $(SUBDIRS); do $(MAKE) clean -C $$dir; done

3ds:
	@$(MAKE) -C 3ds

switch:
	@$(MAKE) -C switch

format:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir format; done

cppcheck:
	cppcheck . --enable=all 2> cppcheck.log

.PHONY: $(SUBDIRS) clean format cppcheck