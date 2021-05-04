CC=gcc
CFLAGS=-Wall -g -ggdb
EXE=test-expect-version

LIBS=

$(EXE): $(EXE).o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f *.o $(EXE)
