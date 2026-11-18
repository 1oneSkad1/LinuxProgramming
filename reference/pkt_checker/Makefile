# Makefile for test C programs

CC	= g++

OBJ	= main.o  util.o packet.o

HEADER	= include.h util.h 
		

LFLAGS  =       -O 
CFLAGS  =       -O -Wno-unused-result  -Wno-return-type -Wno-write-strings


all : t 


t : $(OBJ) 
	$(CC)  -o m  $(OBJ) -O 
	

%.o: %.cc $(HEADER)
	$(CC) $(CFLAGS) -c  $<

	
clean :
	rm -rf *.o core m
