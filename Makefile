# Makefile for the microjson project:

VERSION=0.1

CFLAGS = -O

# Add DEBUG_ENABLE for the tracing code
CFLAGS += -DDEBUG_ENABLE -g

mjson.o: mjson.c mjson.h

test_microjson: test_microjson.o mjson.o
	$(CC) $(CFLAGS) -o test_microjson test_microjson.o mjson.o

# Regression test
check: test_microjson
	test_microjson

make testbuild: mjson.o test_microjson example1 example2 example3

# Worked examples.  These are essentially subsets of the regresion test.
example1: example1.c mjson.c
example2: example2.c mjson.c
example3: example3.c mjson.c

clean:
	rm -f microjson.o test_microjson.o test_microjson example[123]

CSUPPRESSIONS = -U__UNUSED__
cppcheck:
	cppcheck -I. --template gcc --enable=all --suppress=unusedStructMember $(CSUPPRESSIONS) *.[ch]

SOURCES = Makefile *.[ch]
DOCS = README COPYING NEWS control microjson.asc
ALL =  $(SOURCES) $(DOCS)
microjson-$(VERSION).tar.gz: $(ALL)
	tar --transform='s:^:microjson-$(VERSION)/:' --show-transformed-names -cvzf microjson-$(VERSION).tar.gz $(ALL)

dist: microjson-$(VERSION).tar.gz

release: microjson-$(VERS).tar.gz mjson.html
	shipper version=$(VERS) | sh -e -x
