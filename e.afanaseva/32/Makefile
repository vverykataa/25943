CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -pthread
LDLIBS = -lsocket -lrt -lpthread

all: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDLIBS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c $(LDLIBS)

clean:
	rm -f server client

.PHONY: all clean
