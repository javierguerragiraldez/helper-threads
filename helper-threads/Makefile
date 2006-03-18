#
# Helper Threads Toolkit
# (c) 2006 Javier Guerra G.
# $Id: Makefile,v 1.3 2006-03-18 01:44:02 jguerra Exp $
#
 
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


all : helper.so timer.so nb_file.so

helper.o : helper.c helper.h
timer.o : timer.c helper.h

helper.so : helper.o
	ld -o helper.so -shared helper.o -lpthread

timer.so : timer.o
	ld -o timer.so -shared timer.o

nb_file.so : nb_file.o
	ld -o nb_file.so -shared nb_file.o