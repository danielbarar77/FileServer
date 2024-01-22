CC = gcc

all: client server

client: client.c
	$(CC) $^ -g3 -o $@ -lpthread

server: server.c
	$(CC) $^ -g3 -o $@

clean:
	rm client server
