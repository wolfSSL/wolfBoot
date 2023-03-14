LDFLAGS=-lcheck -lm -pthread

UNAME_S := $(shell uname -s)
ifneq ($(UNAME_S),Darwin)
	LDFLAGS+=-lrt -lsubunit
endif

CFLAGS=-I. -I../../src -I../../include -I../../lib/wolfssl
CFLAGS+=-g -ggdb



all: unit-parser unit-extflash unit-aes128 unit-aes256 unit-chacha20


unit-aes128:CFLAGS+=-DEXT_ENCRYPTED -DENCRYPT_WITH_AES128
unit-aes256:CFLAGS+=-DEXT_ENCRYPTED -DENCRYPT_WITH_AES256
unit-chacha20:CFLAGS+=-DEXT_ENCRYPTED -DENCRYPT_WITH_CHACHA

../../include/target.h: FORCE
	cp -f target.h $@

unit-extflash.o: FORCE
	rm -f $@
	gcc -c -o $@ unit-extflash.c $(CFLAGS)

unit-parser: ../../include/target.h unit-parser.o
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-extflash: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-aes128: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-aes256: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

unit-chacha20: ../../include/target.h unit-extflash.c
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

%.o:%.c
	gcc -c -o $@ $^ $(CFLAGS)

clean:
	rm -f unit-parser unit-extflash *.o

.PHONY: FORCE
