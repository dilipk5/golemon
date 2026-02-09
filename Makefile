CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -pthread

all: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c -lutil

clean:
	rm -f server client

.PHONY: all clean
