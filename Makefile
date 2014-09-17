# Makefile for the microjson project:

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

