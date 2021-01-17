# dwm_vol - dynamic menu
# See LICENSE file for copyright and license details.

include config.mk

SRC = drw.c dwm_vol.c util.c
OBJ = $(SRC:.c=.o)

all: options dwm_vol

options:
	@echo dwm_vol build options:
	@echo "CFLAGS   = $(CFLAGS)"
	@echo "LDFLAGS  = $(LDFLAGS)"
	@echo "CC       = $(CC)"

.c.o:
	$(CC) -c $(CFLAGS) $<

config.h:
	cp config.def.h $@

$(OBJ): arg.h config.h config.mk drw.h

dwm_vol: dwm_vol.o drw.o util.o
	$(CC) -o $@ dwm_vol.o drw.o util.o $(LDFLAGS)

clean:
	rm -f dwm_vol $(OBJ) dwm_vol-$(VERSION).tar.gz

dist: clean
	mkdir -p dwm_vol-$(VERSION)
	cp LICENSE Makefile README arg.h config.def.h config.mk dwm_vol.1\
		drw.h util.h $(SRC)\
		dwm_vol-$(VERSION)
	tar -cf dwm_vol-$(VERSION).tar dwm_vol-$(VERSION)
	gzip dwm_vol-$(VERSION).tar
	rm -rf dwm_vol-$(VERSION)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f dwm_vol $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/dwm_vol
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < dwm_vol.1 > $(DESTDIR)$(MANPREFIX)/man1/dwm_vol.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/dwm_vol.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/dwm_vol\
		$(DESTDIR)$(MANPREFIX)/man1/dwm_vol.1\

.PHONY: all options clean dist install uninstall
