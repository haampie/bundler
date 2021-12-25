ifeq (exists, $(shell [ -e $(CURDIR)/Make.user ] && echo exists ))
include $(CURDIR)/Make.user
endif

CFLAGS ?= -O2
LIBTREE_CFLAGS := -std=c99 -Wall -Wextra -Wshadow -pedantic
LIBTREE_DEFINES := -D_FILE_OFFSET_BITS=64

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
SHAREDIR ?= $(PREFIX)/share

.PHONY: all check install clean

all: libtree

%.o: %.c
	$(CC) $(CFLAGS) $(LIBTREE_CFLAGS) $(LIBTREE_DEFINES) -c $?

libtree: libtree.o
	$(CC) $(LDFLAGS) $^ -o $@

check:: libtree

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	cp -p libtree $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(SHAREDIR)/man/man1
	cp -p doc/libtree.1 $(DESTDIR)$(SHAREDIR)/man/man1

clean::
	rm -f *.o libtree

clean check::
	for dir in $(sort $(wildcard tests/*)); do \
		$(MAKE) -C $$dir $@; \
	done
