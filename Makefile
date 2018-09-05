# Copyright (c) 2011 Tim van der Molen <tim@kariliq.nl>
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

.if exists(config.mk)
.include "config.mk"
.endif

PROG=		siren
VERSION!=	cat version
DIST=		${PROG}-${VERSION}

SRCS+=		argv.c bind.c browser.c cache.c command.c conf.c dir.c \
		format.c history.c input.c library.c log.c menu.c msg.c \
		option.c path.c player.c playlist.c plugin.c prompt.c queue.c \
		screen.c siren.c track.c view.c xmalloc.c
OBJS=		${SRCS:S,c$,o,}

IP_SRCS=	${IP:S,^,ip/,:S,$,.c,}
IP_LIBS=	${IP_SRCS:S,.c$,.so,}
IP_OBJS=	${IP_SRCS:S,.c$,.lo,}

OP_SRCS=	${OP:S,^,op/,:S,$,.c,}
OP_LIBS=	${OP_SRCS:S,.c$,.so,}
OP_OBJS=	${OP_SRCS:S,.c$,.lo,}

CC?=		cc
CTAGS?=		ctags
MKDEP?=		mkdep

INSTALL_DIR=	install -dm 755
INSTALL_BIN=	install -m 555
INSTALL_LIB=	install -m 444
INSTALL_MAN=	install -m 444

CFLAGS+=	-Wall -W -Wbad-function-cast -Wcast-align -Wcast-qual \
		-Wformat=2 -Wpointer-arith -Wshadow -Wundef -Wwrite-strings
CPPCHECKFLAGS?=	-I /usr/include -I /usr/local/include --enable=all --force \
		--quiet
MKDEPFLAGS?=	-a

.PHONY: all clean cleandir cleanlog cppcheck depend dist install manlint

.SUFFIXES: .c .lo .o .so

.c.lo:
	${CC} ${CFLAGS} ${CFLAGS_LIB} ${CPPFLAGS} ${CPPFLAGS_${@:T:R}} -c -o \
	    $@ $<

.c.o:
	${CC} ${CFLAGS} ${CPPFLAGS} -c -o $@ $<

.lo.so:
	${CC} -o $@ $< ${LDFLAGS} ${LDFLAGS_LIB} ${LDFLAGS_${@:T:R}}

all: ${PROG} ${IP_LIBS} ${OP_LIBS}

${PROG}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LDFLAGS} ${LDFLAGS_PROG}

.depend: ${SRCS} ${IP_SRCS} ${OP_SRCS} ${PROG}.h
	${MKDEP} ${MKDEPFLAGS} ${CPPFLAGS} ${SRCS}
.for src in ${IP_SRCS} ${OP_SRCS}
	${MKDEP} ${MKDEPFLAGS} ${CPPFLAGS_${src:T:R}} ${src}
.endfor

clean:
	rm -f core *.core ${PROG} ${OBJS}
	rm -f ${IP_LIBS} ${IP_OBJS}
	rm -f ${OP_LIBS} ${OP_OBJS}

cleandir: clean cleanlog
	rm -f .depend tags config.h config.mk

cleanlog:
	rm -f *.log

cppcheck:
	cppcheck ${CPPCHECKFLAGS} *.c */*.c

depend: .depend

dist:
	hg archive -X .hg\* -r ${DIST} ${DIST}
	chmod -R go+rX ${DIST}
	GZIP=-9 tar -czf ${DIST}.tar.gz ${DIST}
	rm -fr ${DIST}
	sha256 ${DIST}.tar.gz > ${DIST}.tar.gz.sha256
	gpg -b ${DIST}.tar.gz

install:
	${INSTALL_DIR} ${DESTDIR}${BINDIR}
	${INSTALL_DIR} ${DESTDIR}${MANDIR}/man1
	${INSTALL_DIR} ${DESTDIR}${PLUGINDIR}
	${INSTALL_DIR} ${DESTDIR}${PLUGINDIR}/ip
	${INSTALL_DIR} ${DESTDIR}${PLUGINDIR}/op
	${INSTALL_BIN} ${PROG} ${DESTDIR}${BINDIR}
	${INSTALL_MAN} ${PROG}.1 ${DESTDIR}${MANDIR}/man1
.if !empty(IP_LIBS)
	${INSTALL_LIB} ${IP_LIBS} ${DESTDIR}${PLUGINDIR}/ip
.endif
.if !empty(OP_LIBS)
	${INSTALL_LIB} ${OP_LIBS} ${DESTDIR}${PLUGINDIR}/op
.endif

manlint:
	mandoc -Tlint -Wstyle ${PROG}.1

tags: ${SRCS} ${IP_SRCS} ${OP_SRCS} ${PROG}.h
	${CTAGS} -dtw ${SRCS} ${IP_SRCS} ${OP_SRCS}

uninstall:
	rm -f ${DESTDIR}${BINDIR}/${PROG}
	rm -f ${DESTDIR}${MANDIR}/man1/${PROG}.1
	rm -fr ${DESTDIR}${PLUGINDIR}
