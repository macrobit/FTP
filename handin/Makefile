CC = gcc

LIBS =  /home/courses/cse533/Stevens/unpv13e_solaris2.10/libunp.a -lpthread -lm -lsocket -lresolv -lnsl

FLAGS =  -g -O2
CFLAGS = ${FLAGS} -I/home/courses/cse533/Stevens/unpv13e_solaris2.10/lib

SOURCES = get_ifi_info_plus.c get_ifi.c data.c rtt.c
OBJS = $(SOURCES:.c=.o)

all: server client

server: $(OBJS) server.o
	${CC} -o $@ $^ ${LIBS}

client: $(OBJS) client.o
	${CC} -o $@ $^ ${LIBS}

.c.o:
	${CC} ${CFLAGS} -c $< -o $@

clean:
	rm *.o
	rm server client
