#ifndef DATA_H
#define DATA_H

#include "unp.h"

#define BUFFER_SIZE 512
#define HEADER_SIZE 16
#define SYN 1
#define ACK 2
#define DAT 4
#define FIN 8
#define RETRY 16

struct datagram {
    char buf[BUFFER_SIZE];
};

int make_datagram(struct datagram* gram, int flag, int seq, int ack, int ts, char* buf, ssize_t buflen);
int get_header(struct datagram* gram, int* pflag, int* pseq, int* pack, int* pts);
int get_payload(struct datagram* gram, char* buf, ssize_t buflen);
char* get_payload_ptr(struct datagram* gram);
int get_flag(struct datagram* gram);
void set_flag(struct datagram* gram, int flag);
void set_retry_flag(struct datagram* gram);
void set_fin_flag(struct datagram* gram);
void get_str(struct datagram* gram, char* buf, ssize_t buflen);
void set_ts(struct datagram* gram, int ts);

#endif
