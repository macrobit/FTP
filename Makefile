CC = gcc

LIBS =  /home/figo/CourseProjects/NP/unpv13e/libunp.a -lpthread -lm

FLAGS =  -g -O2
CFLAGS = ${FLAGS} -I/home/figo/CourseProjects/NP/unpv13e/lib

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
	rm prifinfo_plus prifinfo_plus.o get_ifi_info_plus.o
