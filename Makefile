# dvol - dynamic menu
# See LICENSE file for copyright and license details.

include config.mk

SRC = drw.c dvol.c util.c
OBJ = $(SRC:.c=.o)

all: options dvol

options:
	@echo dvol build options:
	@echo "CFLAGS   = $(CFLAGS)"
	@echo "LDFLAGS  = $(LDFLAGS)"
	@echo "CC       = $(CC)"

.c.o:
	$(CC) -c $(CFLAGS) $<

config.h:
	cp config.def.h $@

$(OBJ): arg.h config.h config.mk drw.h

dvol: dvol.o drw.o util.o
	$(CC) -o $@ dvol.o drw.o util.o $(LDFLAGS)

clean:
	rm -f dvol $(OBJ) dvol-$(VERSION).tar.gz

dist: clean
	mkdir -p dvol-$(VERSION)
	cp LICENSE Makefile README arg.h config.def.h config.mk dvol.1\
		drw.h util.h $(SRC)\
		dvol-$(VERSION)
	tar -cf dvol-$(VERSION).tar dvol-$(VERSION)
	gzip dvol-$(VERSION).tar
	rm -rf dvol-$(VERSION)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f dvol $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/dvol
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < dvol.1 > $(DESTDIR)$(MANPREFIX)/man1/dvol.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/dvol.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/dvol\
		$(DESTDIR)$(MANPREFIX)/man1/dvol.1\

.PHONY: all options clean dist install uninstall
