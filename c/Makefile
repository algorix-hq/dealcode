# dealcode C library.
#
# Targets:
#   all      (default) build the static library libdealcode.a
#   test     build and run the test suite (requires python3 for vector codegen)
#   install  install the static library, header, and pkg-config file
#            (honors PREFIX, default /usr/local, and DESTDIR for staging)
#   clean    remove build artifacts
#
# Flag seams:
#   CFLAGS        optimization/debug flags (default -O2). The flags the build
#                 requires (-std=c11, warnings, -Iinclude) are appended via
#                 `override`, so `make test CFLAGS=-g` cannot break the build.
#   EXTRA_CFLAGS  appended last; used for sanitizers and similar, e.g.:
#                 make test EXTRA_CFLAGS='-fsanitize=address,undefined -g'
#                 (EXTRA_CFLAGS also reach the test link, so -fsanitize works.)
#
# Requires OpenSSL libcrypto; consumers must link with -lcrypto.

CC      ?= cc
CFLAGS  ?= -O2
override CFLAGS += -std=c11 -Wall -Wextra -Werror -Iinclude $(EXTRA_CFLAGS)
LDLIBS   = -lcrypto
PYTHON  ?= python3
BUILDDIR = build

# Single source of truth for the version: DEALCODE_VERSION in dealcode.h.
VERSION := $(shell sed -n 's/.*DEALCODE_VERSION "\([0-9][0-9.]*\)".*/\1/p' \
                include/dealcode.h)

PREFIX       ?= /usr/local
LIBDIR       ?= $(PREFIX)/lib
INCLUDEDIR   ?= $(PREFIX)/include
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig
INSTALL      ?= install

.PHONY: all test install clean

all: libdealcode.a

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/dealcode.o: src/dealcode.c include/dealcode.h src/ff1.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ src/dealcode.c

libdealcode.a: $(BUILDDIR)/dealcode.o
	ar rcs $@ $^

$(BUILDDIR)/vectors.inc: tests/gen_vectors.py ../testvectors/ff1_nist.json ../testvectors/v1.json | $(BUILDDIR)
	$(PYTHON) tests/gen_vectors.py $@

$(BUILDDIR)/test_dealcode: tests/test_dealcode.c $(BUILDDIR)/vectors.inc libdealcode.a include/dealcode.h src/ff1.h
	$(CC) $(CFLAGS) -Isrc -I$(BUILDDIR) -o $@ tests/test_dealcode.c libdealcode.a $(LDLIBS)

test: $(BUILDDIR)/test_dealcode
	./$(BUILDDIR)/test_dealcode

# Regenerated on every install so PREFIX/LIBDIR changes are never stale.
$(BUILDDIR)/dealcode.pc: include/dealcode.h Makefile FORCE | $(BUILDDIR)
	@{ echo 'prefix=$(PREFIX)'; \
	   echo 'exec_prefix=$${prefix}'; \
	   echo 'libdir=$(LIBDIR)'; \
	   echo 'includedir=$(INCLUDEDIR)'; \
	   echo ''; \
	   echo 'Name: dealcode'; \
	   echo 'Description: Bijective counter <-> code mapping via FF1 (dealcode format v1)'; \
	   echo 'Version: $(VERSION)'; \
	   echo 'Requires.private: libcrypto'; \
	   echo 'Libs: -L$${libdir} -ldealcode'; \
	   echo 'Cflags: -I$${includedir}'; } > $@

FORCE:
.PHONY: FORCE

install: libdealcode.a $(BUILDDIR)/dealcode.pc
	$(INSTALL) -d $(DESTDIR)$(LIBDIR) $(DESTDIR)$(INCLUDEDIR) $(DESTDIR)$(PKGCONFIGDIR)
	$(INSTALL) -m 644 libdealcode.a $(DESTDIR)$(LIBDIR)/libdealcode.a
	$(INSTALL) -m 644 include/dealcode.h $(DESTDIR)$(INCLUDEDIR)/dealcode.h
	$(INSTALL) -m 644 $(BUILDDIR)/dealcode.pc $(DESTDIR)$(PKGCONFIGDIR)/dealcode.pc

clean:
	rm -rf $(BUILDDIR) libdealcode.a
