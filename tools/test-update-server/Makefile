CC=gcc
CFLAGS=-Wall -g -ggdb
EXE=server

LIBS=-lpthread

$(EXE): $(EXE).o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f *.o $(EXE)
