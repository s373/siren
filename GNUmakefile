# Copyright (c) 2011 Tim van der Molen <tbvdm@xs4all.nl>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

ifneq ($(wildcard config.mk),)
include config.mk
endif

PROG=		siren
VERSION=	$(shell cat version)
DIST=		${PROG}-${VERSION}

SRCS+=		argv.c bind.c browser.c cache.c command.c conf.c dir.c \
		format.c history.c input.c library.c log.c menu.c msg.c \
		option.c path.c player.c playlist.c plugin.c prompt.c queue.c \
		screen.c siren.c track.c view.c xmalloc.c xpathconf.c
OBJS=		${SRCS:.c=.o}

IP_SRCS=	$(addprefix ip/, $(addsuffix .c, ${IP}))
IP_LIBS=	${IP_SRCS:.c=.so}
IP_OBJS=	${IP_SRCS:.c=.o}

OP_SRCS=	$(addprefix op/, $(addsuffix .c, ${OP}))
OP_LIBS=	${OP_SRCS:.c=.so}
OP_OBJS=	${OP_SRCS:.c=.o}

CC?=		cc
CTAGS?=		ctags
MKDEP?=		mkdep

INSTALL_DIR=	install -dm 755
INSTALL_BIN=	install -m 555
INSTALL_LIB=	install -m 444
INSTALL_MAN=	install -m 444

CFLAGS+=	-Wall -W -Wbad-function-cast -Wcast-align -Wcast-qual \
		-Wformat=2 -Wpointer-arith -Wshadow -Wundef -Wwrite-strings
LDFLAGS+=	-lcurses -pthread -Wl,--export-dynamic
MKDEPFLAGS?=	-a

.PHONY: all clean cleandir cleanlog cppcheck depend dist install

ip/%.o: ip/%.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${CPPFLAGS_$(*F)} -fPIC -c -o $@ $<

ip/%.so: ip/%.o
	${CC} -fPIC -shared -o $@ $< ${LDFLAGS_$(*F)}

op/%.o: op/%.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${CPPFLAGS_$(*F)} -fPIC -c -o $@ $<

op/%.so: op/%.o
	${CC} -fPIC -shared -o $@ $< ${LDFLAGS_$(*F)}

%.o: %.c
	${CC} ${CFLAGS} ${CPPFLAGS} -c -o $@ $<

all: ${PROG} ${IP_LIBS} ${OP_LIBS}

${PROG}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LDFLAGS}

.depend: ${SRCS} ${IP_SRCS} ${OP_SRCS} ${PROG}.h
	${MKDEP} ${MKDEPFLAGS} ${CPPFLAGS} ${SRCS}
	${MKDEP} ${MKDEPFLAGS} $(foreach p, ${IP}, ${CPPFLAGS_$p}) ${IP_SRCS}
	${MKDEP} ${MKDEPFLAGS} $(foreach p, ${OP}, ${CPPFLAGS_$p}) ${OP_SRCS}

clean:
	rm -f core *.core ${PROG} ${OBJS}
	rm -f ${IP_LIBS} ${IP_OBJS}
	rm -f ${OP_LIBS} ${OP_OBJS}

cleandir: clean cleanlog
	rm -f .depend tags config.h config.mk

cleanlog:
	rm -f *.log

cppcheck:
	cppcheck --enable=all --quiet ${SRCS} ${IP_SRCS} ${OP_SRCS}

depend: .depend

dist:
	hg archive -X .hg\* -r ${DIST} ${DIST}
	chmod -R go+rX ${DIST}
	GZIP=-9 tar -czf ${DIST}.tar.gz ${DIST}
	rm -fr ${DIST}
	sha256 ${DIST}.tar.gz > ${DIST}.tar.gz.sha256
	gpg -bu 4cdfe96f ${DIST}.tar.gz

install:
	${INSTALL_DIR} ${DESTDIR}${BINDIR}
	${INSTALL_DIR} ${DESTDIR}${MANDIR}/man1
	${INSTALL_DIR} ${DESTDIR}${PLUGINDIR}
	${INSTALL_DIR} ${DESTDIR}${PLUGINDIR}/ip
	${INSTALL_DIR} ${DESTDIR}${PLUGINDIR}/op
	${INSTALL_BIN} ${PROG} ${DESTDIR}${BINDIR}
	${INSTALL_MAN} ${PROG}.1 ${DESTDIR}${MANDIR}/man1
ifneq (${IP_LIBS},)
	${INSTALL_LIB} ${IP_LIBS} ${DESTDIR}${PLUGINDIR}/ip
endif
ifneq (${OP_LIBS},)
	${INSTALL_LIB} ${OP_LIBS} ${DESTDIR}${PLUGINDIR}/op
endif

tags: ${SRCS} ${IP_SRCS} ${OP_SRCS} ${PROG}.h
	${CTAGS} -dtw ${SRCS} ${IP_SRCS} ${OP_SRCS}

uninstall:
	rm -f ${DESTDIR}${BINDIR}/${PROG}
	rm -f ${DESTDIR}${MANDIR}/man1/${PROG}.1
	rm -fr ${DESTDIR}${PLUGINDIR}
