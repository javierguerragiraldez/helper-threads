# Compilation parameters
CC = gcc
CWARNS = -Wall -pedantic \
        -Waggregate-return \
        -Wcast-align \
        -Wmissing-prototypes \
        -Wstrict-prototypes \
        -Wnested-externs \
        -Wpointer-arith \
        -Wshadow \
        -Wwrite-strings


CFLAGS = $(CONFIG) $(CWARNS) -ansi -g -O2 -I/usr/local/include/lua5


all : helper.so timer.so

helper.o : helper.c helper.h
timer.o : timer.c helper.h

helper.so : helper.o
	ld -o helper.so -shared helper.o -lpthread

timer.so : timer.o
	ld -o timer.so -shared timer.o

