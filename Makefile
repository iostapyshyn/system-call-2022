TOPTARGETS := all clean

SUBDIRS := 01-open 02-clone 03-mmap 04-futex 05-inotify 06-sigaction

$(TOPTARGETS): $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY: $(TOPTARGETS) $(SUBDIRS)
