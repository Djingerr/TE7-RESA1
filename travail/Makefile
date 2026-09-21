CC = gcc
CFLAGS = -Wall -Werror -Iinclude
#LDFLAGS=-lpthread
all: client server

client: client.o
	$(CC) $(CFLAGS) -o client client.o

serveur: serveur.o
	$(CC) $(CFLAGS) -o server server.o

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

serveur.o: serveur.c
	$(CC) $(CFLAGS) -c server.c

clean:
	rm -f client server client.o serveur.o

#.PHONY all clean
