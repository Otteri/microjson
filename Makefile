# Makefile for the microjson project:

VERSION=1.0

CFLAGS = -O

# Add DEBUG_ENABLE for the tracing code
CFLAGS += -DDEBUG_ENABLE -g

all: mjson.o test_microjson example1 example2 example3
	@test_microjson

mjson.o: mjson.c mjson.h

test_microjson: test_microjson.o mjson.o
	$(CC) $(CFLAGS) -o test_microjson test_microjson.o mjson.o

.SUFFIXES: .html .asc

# Requires asciidoc and xsltproc/docbook stylesheets.
.asc.html:
	asciidoc $*.asc

# Regression test
check: test_microjson
	test_microjson

# Worked examples.  These are essentially subsets of the regresion test.
example1: example1.c mjson.c
example2: example2.c mjson.c
example3: example3.c mjson.c

clean:
	rm -f microjson.o test_microjson.o test_microjson example[123]
	rm -f microjson.html

CSUPPRESSIONS = -U__UNUSED__
cppcheck:
	cppcheck -I. --template gcc --enable=all --suppress=unusedStructMember $(CSUPPRESSIONS) *.[ch]

SOURCES = Makefile *.[ch]
DOCS = README COPYING NEWS control microjson.asc
ALL =  $(SOURCES) $(DOCS)
microjson-$(VERSION).tar.gz: $(ALL)
	tar --transform='s:^:microjson-$(VERSION)/:' --show-transformed-names -cvzf microjson-$(VERSION).tar.gz $(ALL)

dist: microjson-$(VERSION).tar.gz

release: microjson-$(VERSION).tar.gz microjson.html
	shipper version=$(VERSION) | sh -e -x
