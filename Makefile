VERSION	= 4.0

CC		= gcc
CFLAGS		= -c -Wall -g -O2   \
		  `pkg-config --cflags libxmp audacious gtk+-3.0 glib-2.0`
LD		= gcc
LDFLAGS		=  \
		  `pkg-config --libs libxmp audacious gtk+-3.0 glib-2.0`
LIBS		= 
INSTALL		= /usr/bin/install -c
PLUGIN_DIR	= `pkg-config --variable input_plugin_dir audacious`
DESTDIR		=
SHELL		= /bin/sh
V		= 0

DIST		= audacious-plugin-xmp-$(VERSION)
DFILES		= README INSTALL COPYING CREDITS Changelog configure \
		  install-sh configure.ac Makefile.in
DDIRS		=

all: binaries

CFLAGS += -I. -DVERSION=\"$(VERSION)\"

.SUFFIXES: .c .o .lo .a .so .dll

.c.o:
	@CMD='$(CC) $(CFLAGS) -o $*.o $<'; \
	if [ "$(V)" -gt 0 ]; then echo $$CMD; else echo CC $*.o ; fi; \
	eval $$CMD

binaries: xmp-audacious3.so

xmp-audacious3.so: audacious3.o

clean:
	@rm -f $(OBJS)

install: install-plugin

install-plugin:
	$(INSTALL)

depend:
	@echo Building dependencies...
	@$(CC) $(CFLAGS) -MM $(OBJS:.o=.c) > $@
	    
dist: dist-prepare dist-subdirs

dist-prepare:
	./config.status
	rm -Rf $(DIST) $(DIST).tar.gz
	mkdir -p $(DIST)
	cp -RPp $(DFILES) $(DIST)/

dist-subdirs: $(addprefix dist-,$(DDIRS))
	chmod -R u+w $(DIST)/*
	tar cvf - $(DIST) | gzip -9c > $(DIST).tar.gz
	rm -Rf $(DIST)
	ls -l $(DIST).tar.gz

$(OBJS): Makefile

sinclude depend

