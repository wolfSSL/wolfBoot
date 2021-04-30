CC=gcc
CFLAGS=-Wall -g -ggdb
EXE=bin-assemble

LIBS=

$(EXE): $(EXE).o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f *.o $(EXE)
