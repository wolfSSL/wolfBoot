all: bmdiff bmpatch


bmdiff: delta.o
	gcc -o bmdiff delta.o

bmpatch: delta.o
	gcc -o bmpatch delta.o

lib: delta.o

delta.o:
	gcc -c -o delta.o ../../src/delta.c -I../../include -DBM_TEST_MAIN -ggdb

clean:
	rm -f bmpatch bmdiff delta.o
