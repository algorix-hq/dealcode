# dealcode C library.
#
# Targets:
#   all   (default) build the static library libdealcode.a
#   test  build and run the test suite (requires python3 for vector codegen)
#   clean remove build artifacts
#
# Requires OpenSSL libcrypto; consumers must link with -lcrypto.

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Werror -Iinclude
LDLIBS   = -lcrypto
PYTHON  ?= python3
BUILDDIR = build

.PHONY: all test clean

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

clean:
	rm -rf $(BUILDDIR) libdealcode.a
