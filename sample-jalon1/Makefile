CC = gcc
CFLAGS=-Wall -Werror
#LDFLAGS=-lpthread
all: client server

client: client.o
	$(CC) $(CFLAGS) -o client client.o

serveur: serveur.o
	$(CC) $(CFLAGS) -o serveur serveur.o

client.o: client.c common.h
	$(CC) $(CFLAGS) -c client.c

serveur.o: serveur.c common.h  #en cas de modification de common il est recompilé sinon il reste tel quel.
	$(CC) $(CFLAGS) -c serveur.c

clean:
	rm -f client server client.o serveur.o

	.PHONY all clean
	
