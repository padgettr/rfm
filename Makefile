# Makefile for RFM - Rod's File Manager

# Uncomment the next line for GTK+-2 build (requires latest version 2.24.23)
#USE_GTK2 = True
VERSION = 1.5.9

ifdef USE_GTK2
GTK_VERSION = gtk+-2.0
CPPFLAGS = -DRFM_USE_GTK2
else
GTK_VERSION = gtk+-3.0
CPPFLAGS =
endif

SRC = rfm.c
OBJ = ${SRC:.c=.o}
INCS = -I. -I/usr/include
LIBS = -L/usr/lib -lc `pkg-config --libs ${GTK_VERSION}`
CPPFLAGS += -DVERSION=\"${VERSION}\"
GTK_CFLAGS = `pkg-config --cflags ${GTK_VERSION}`
CFLAGS = -g -Wall -O0 ${GTK_CFLAGS} ${INCS} ${CPPFLAGS}
LDFLAGS = -g ${LIBS}
PREFIX = /usr/local

# compiler and linker
CC = gcc

all: options rfm

options:
	@echo rfm build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h

config.h:
	@echo creating $@ from config.def.h
	@cp config.def.h $@

rfm: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f rfm ${OBJ}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${PREFIX}/bin
	@cp -f rfm ${PREFIX}/bin
	@chmod 755 ${PREFIX}/bin/rfm

uninstall:
	@echo removing executable file from ${PREFIX}/bin
	@rm -f ${PREFIX}/bin/rfm

.PHONY: all options clean install uninstall
