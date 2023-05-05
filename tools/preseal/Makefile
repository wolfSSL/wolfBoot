CC = gcc

all: preseal

preseal:
	$(Q)$(CC) -o $@ $@.c -lwolftpm

debug:
	$(Q)$(CC) -o preseal preseal.c -g -lwolftpm -lwolfssl

clean:
	rm -f preseal
