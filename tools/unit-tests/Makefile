LDFLAGS=-lcheck -lm -pthread

UNAME_S := $(shell uname -s)
ifneq ($(UNAME_S),Darwin)
	LDFLAGS+=-lrt -lsubunit
endif

CFLAGS=-I. -I../../src -I../../include -I../../lib/wolfssl
CFLAGS+=-g -ggdb

all: unit-parser unit-extflash

../../include/target.h: FORCE
	cp -f target.h $@

unit-parser: ../../include/target.h unit-parser.o
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-extflash: ../../include/target.h unit-extflash.o
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

%.o:%.c
	gcc -c -o $@ $^ $(CFLAGS)

clean:
	rm -f unit-parser unit-extflash *.o

FORCE:
