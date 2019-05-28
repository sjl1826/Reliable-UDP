.SILENT:

objects = server client
all: clean $(objects)

server:
	gcc -o server server.c

client:
	gcc -o client client.c

clean:
	rm client server
