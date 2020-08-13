LDFLAGS=-lcheck -lm -pthread

UNAME_S := $(shell uname -s)
ifneq ($(UNAME_S),Darwin)
	LDFLAGS+=-lrt -lsubunit
endif

CFLAGS=-I../../src -I../../include

all: unit-parser


unit-parser: unit-parser.o
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

%.o:%.c
	gcc -c -o $@ $^ $(CFLAGS)

clean:
	rm -f unit-parser unit-parser.o
