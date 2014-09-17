# Makefile for the microjson project:

VERSION=0.1

CFLAGS = -O

# Add DEBUG_ENABLE for the tracing code
# CFLAGS += -DDEBUG_ENABLE

microjson.o: microjson.c microjson.h

test_microjson: test_microjson.o microjson.o
	$(CC) -o test_microjson test_microjson.o microjson.o

check: test_microjson
	test_microjson

clean:
	rm -f microjson.o test_json

CSUPPRESSIONS = -U__UNUSED__
cppcheck:
	cppcheck -I. --template gcc --enable=all --suppress=unusedStructMember $(CSUPPRESSIONS) *.[ch]

SOURCES = Makefile *.[ch]
DOCS = README COPYING
ALL =  $(SOURCES) $(DOCS)
microjson-$(VERSION).tar.gz: $(ALL)
	tar --transform='s:^:microjson-$(VERSION)/:' --show-transformed-names -cvzf microjson-$(VERSION).tar.gz $(ALL)

dist: microjson-$(VERSION).tar.gz

