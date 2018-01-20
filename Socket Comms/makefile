CC=gcc
CFLAGS=-Wall -Wextra -Wpedantic -std=gnu99 -g

server: server.c
	$(CC) $(CFLAGS) -o server server.c

client: client.o
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f *.o client server
