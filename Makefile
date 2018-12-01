SUBDIRS = 3ds switch

all:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir; done

clean:
	@for dir in $(SUBDIRS); do $(MAKE) clean -C $$dir; done

nds:
	$(MAKE) -C 3ds

nx:
	$(MAKE) -C switch
