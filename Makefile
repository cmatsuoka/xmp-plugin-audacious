VERSION	= 4.0.0

CC		= gcc
CFLAGS		= -c -Wall -g -O2 -fPIC -D_REENTRANT \
		  `pkg-config --cflags libxmp audacious glib-2.0`
LD		= gcc
LDFLAGS		= 
LIBS		= `pkg-config --libs libxmp audacious glib-2.0`
INSTALL		= /usr/bin/install -c
PLUGIN_DIR	= `pkg-config --variable plugin_dir audacious`/Input
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

.c.lo:
	@CMD='$(CC) $(CFLAGS) -fPIC -o $*.lo $<'; \
	if [ "$(V)" -gt 0 ]; then echo $$CMD; else echo CC $*.lo ; fi; \
	eval $$CMD

binaries: xmp-audacious3.so

xmp-audacious3.so: audacious3.lo
	@CMD='$(LD) -shared -o $@ $(LDFLAGS) $+ $(LIBS)'; \
	if [ "$(V)" -gt 0 ]; then echo $$CMD; else echo LD $@ ; fi; \
	eval $$CMD

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

