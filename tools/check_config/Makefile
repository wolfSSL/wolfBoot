# Configuration values: see tools/config.mk
-include ../../.config
include ../config.mk
include ../../options.mk
CFLAGS+=-I. -I../../ -I../../include -m32

check_config: check_config.o
	@gcc -o $@ $^ $(CFLAGS)
	@./check_config

%.o:%.c
	@gcc -c -o $@ $^ $(CFLAGS)

clean:
	@rm -f check_config *.o
