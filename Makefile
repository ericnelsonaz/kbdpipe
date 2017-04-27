prefix ?= /usr/local

all: kbdpipe

clean:
	rm -f *.o kbdpipe

kbdpipe: kbdpipe.c
	$(CC) -O3 -Wall -o kbdpipe kbdpipe.c

install: all
	cp -fv kbdpipe ${prefix}/sbin/
