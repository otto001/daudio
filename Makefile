# daudio - dynamic menu
# See LICENSE file for copyright and license details.

include config.mk

SRC = drw.c daudio.c util.c
OBJ = $(SRC:.c=.o)

all: options daudio

options:
	@echo daudio build options:
	@echo "CFLAGS   = $(CFLAGS)"
	@echo "LDFLAGS  = $(LDFLAGS)"
	@echo "CC       = $(CC)"

.c.o:
	$(CC) -c $(CFLAGS) $<

config.h:
	cp config.def.h $@

$(OBJ): arg.h config.h config.mk drw.h

daudio: daudio.o drw.o util.o
	$(CC) -o $@ daudio.o drw.o util.o $(LDFLAGS)

clean:
	rm -f daudio $(OBJ) daudio-$(VERSION).tar.gz

dist: clean
	mkdir -p daudio-$(VERSION)
	cp LICENSE Makefile README arg.h config.def.h config.mk daudio.1\
		drw.h util.h $(SRC)\
		daudio-$(VERSION)
	tar -cf daudio-$(VERSION).tar daudio-$(VERSION)
	gzip daudio-$(VERSION).tar
	rm -rf daudio-$(VERSION)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f daudio $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/daudio
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < daudio.1 > $(DESTDIR)$(MANPREFIX)/man1/daudio.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/daudio.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/daudio\
		$(DESTDIR)$(MANPREFIX)/man1/daudio.1\

.PHONY: all options clean dist install uninstall
