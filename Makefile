CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -pthread

# Module source files
MODULE_SRCS = modules/shell.c modules/persistence.c modules/firefox_dump.c modules/chrome_dump.c

all: server client agent

server: server.c $(MODULE_SRCS)
	$(CC) $(CFLAGS) -o server server.c $(MODULE_SRCS) $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c -lutil -ldl -lsqlite3 -lcurl

agent: agent.c
	$(CC) $(CFLAGS) -o agent agent.c -lutil -lcurl

demo: demo.c
	$(CC) $(CFLAGS) -o demo demo.c -lcurl

clean:
	rm -f server client agent demo

.PHONY: all clean
