CC = gcc
CFLAGS = -Wall
TARGETS = client server

all: $(TARGETS)

err.o: err.c err.h

client.o: client.c err.h

client: client.o err.o

server: server.o

clean:
	rm -f *.o $(TARGETS)
