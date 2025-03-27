#Very simple Makefile
#Author: A goltsev
#May 18, 2011 
CC = /usr/bin/gcc
ttt : tttserver.o tttclient.o
	$(CC) -o tttserver tttserver.o && $(CC) -o tttclient -lncurses tttclient.o
tttserver : tttserver.o
	$(CC) -o tttserver tttserver.o
tttclient : tttclient.o
	$(CC) -o tttclient tttclient.o
tttserver.o : tttserver.c ttt.h
	$(CC) -c tttserver.c
tttclient.o : tttclient.c ttt.h
	$(CC) -c tttclient.c
clean:
	\rm *.o
