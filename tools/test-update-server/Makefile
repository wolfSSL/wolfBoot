CC=gcc
CFLAGS=-Wall -DWOLFSSL_DEBUG -DTFM_TIMING_RESISTANT -g -ggdb
EXE=server

LIBS=-lwolfssl -lpthread

$(EXE): $(EXE).o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f *.o $(EXE)
