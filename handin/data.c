#include "data.h"

int make_datagram(struct datagram* gram, int flag, int seq, int ack, int ts, char* buf, ssize_t buflen) {
    if (!gram || buflen > (BUFFER_SIZE-HEADER_SIZE))
        return 0;

    memset(gram, 0, sizeof(struct datagram));
    *(int*)gram->buf = flag;
    *(((int*)gram->buf)+1) = seq;
    *(((int*)gram->buf)+2) = ack;
    *(((int*)gram->buf)+3) = ts;
    if (buf && buflen)
        memcpy(gram->buf+HEADER_SIZE, buf, buflen);

    return 1;
}

int get_header(struct datagram* gram, int* pflag, int* pseq, int* pack, int* pts) {
    if (!gram)
        return 0;

    if (pflag)
        *pflag = *(int*)gram->buf;

    if (pseq)
        *pseq = *(((int*)gram->buf)+1);

    if (pack)
        *pack = *(((int*)gram->buf)+2);

    if (pts)
        *pts = *(((int*)gram->buf)+3);
    
    return 1;
}

int get_payload(struct datagram* gram, char* buf, ssize_t buflen) {
    if (!gram || !buf || buflen > (BUFFER_SIZE-HEADER_SIZE))
        return 0;

    if (buf && buflen)
        memcpy(buf, gram->buf+HEADER_SIZE, buflen);

    return 1;
}

char* get_payload_ptr(struct datagram* gram) {
    if (!gram)
        return NULL;

    return gram->buf + HEADER_SIZE;
}

int get_flag(struct datagram* gram) {
    if (!gram)
        return 0;

    return *(int*)gram->buf;
}

void set_flag(struct datagram* gram, int flag) {
    if (!gram)
        return;

    *(int*)gram->buf = flag;
}

void set_retry_flag(struct datagram* gram) {
    if (!gram)
        return;

    *(int*)gram->buf |= RETRY;
}

void set_fin_flag(struct datagram* gram) {
    if (!gram)
        return;

    *(int*)gram->buf |= FIN;
}

void set_ts(struct datagram* gram, int ts)  {
    if (!gram)
        return;

    *(((int*)gram->buf)+3) = ts;
}


void get_str(struct datagram* ptr_gram, char* buf, ssize_t buflen) {
    int flag;
    int seq;
    int ack;

    get_header(ptr_gram, &flag, &seq, &ack, 0);
    bzero(buf, buflen);

    if (flag & SYN)
        sprintf(buf+strlen(buf), "SYN ");

    if (flag & ACK)
        sprintf(buf+strlen(buf), "ACK ");

    if (flag & DAT) 
        sprintf(buf+strlen(buf), "DAT ");

    if (flag & FIN)
        sprintf(buf+strlen(buf), "FIN ");

    sprintf(buf+strlen(buf), "seq: %d ", seq);

    if (flag & ACK)
        sprintf(buf+strlen(buf), "ack: %d", ack);
}
