
CC=gcc
CFLAGS=-g -O2 -std=gnu89 -Wall -Wpointer-arith -Wstrict-prototypes -MMD
LIBFLAGS=-I. -shared -fPIC
LIBS=-L. -lbuddy
LIBOBJS=buddy.o

all: libbuddy.so libbuddy.a

buddy.o: buddy.c
	$(CC) $(CFLAGS) -shared -fPIC -c -o $@ $?

libbuddy.so: $(LIBOBJS)
	$(LD) $(LIBFLAGS) -o $@ $?

libbuddy.a: $(LIBOBJS)
	$(AR)  rcv $@ $(LIBOBJS)
	ranlib $@

clean:	
	/bin/rm -f *.o a.out buddy-test malloc-test libbuddy.* buddy-unit-test
