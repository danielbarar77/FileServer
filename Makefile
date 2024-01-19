CC = gcc

all: client server

client: client.c
	rm client
	$(CC) $^ -g3 -o $@ -lpthread

server: server.c
	rm server
	$(CC) $^ -g3 -o $@

clean:
	rm client server
