# Makefile for client-server example of Fig. 6-6
all:	
	cc -o client Fig-6-6-client.c -lsocket -lnsl
	cc -o server Fig-6-6-server.c -lsocket -lnsl
