#include <stdio.h>
#include "get_ifi.h"
#include "data.h"
#include "unprtt.h"

static int port = 6825;
static int windowsize = 25;
static int recv_windowsize = 0;

static struct ifi_info *addrs[MAX_IFI_COUNT];
static int sockfds[MAX_IFI_COUNT];
static int count = 0;

void sigchld(int sig) {
    pid_t p;
    int status;

    while ((p=waitpid(-1, &status, WNOHANG)) != -1) {
    }
}

int readfile() {
    FILE* file;
    int fd;
    ssize_t len;
    char tmp[MAXLINE];

    file = fopen("server.in", "r");
    if (!file)
        return 0;
    fd = fileno(file);
    len = Readline(fd, tmp, MAXLINE);
    if (!len)
        goto error;
    tmp[len-1]=0;
    port = atoi(tmp);
    len = Readline(fd, tmp, MAXLINE);
    if (!len)
        goto error;
    tmp[len-1]=0;
    windowsize = atoi(tmp);
    if (windowsize < 1)
        windowsize = 1;

    fclose(file);
    return 1;
error:
    fclose(file);
    return 0;
}

void send_to(int sockfd, struct datagram* ptr_gram,
             const struct sockaddr_in *dest_addr, socklen_t addrlen) {
    char buf[MAXLINE];

    Sendto(sockfd, ptr_gram->buf, BUFFER_SIZE, 0, (SA*)dest_addr, addrlen);

    get_str(ptr_gram, buf, MAXLINE);

    printf("Send to client[%s:%d] %s\n", inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port), buf);
}

ssize_t recv_from(int sockfd, struct datagram* ptr_gram,
             const struct sockaddr_in *dest_addr, socklen_t* addrlen) {
    char buf[MAXLINE];
    int recv_flag;
    char* payload;

    Recvfrom(sockfd, ptr_gram->buf, BUFFER_SIZE, 0, (SA*)dest_addr, addrlen);

    get_header(ptr_gram, &recv_flag, 0, 0, 0);
    get_str(ptr_gram, buf, MAXLINE);

    if ((recv_flag & ACK) && !(recv_flag & SYN)) {
        payload = get_payload_ptr(ptr_gram);
        sprintf(buf+strlen(buf), " winsize: %d", *(int*)payload);
    }

    printf("Receive from client[%s:%d] %s\n", inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port), buf);
}

void child(int index, struct sockaddr_in* ptr_connaddr, struct datagram* ptr_conn_gram) {
    struct datagram recv_gram;
    struct datagram send_gram;
    struct datagram* sendbuf;
    uint32_t* sendtv;
    char *tmp;
    int sockfd;
    int nready;
    int i;
    fd_set rset, allset;
    int maxfdp1 = 0;
    struct sockaddr_in servaddr, cliaddr;
    int local = 0;
    int on = 1;
    int len;
    //int filepos = -1;
    int filesize = 0;
    int servsockclosed = 0;
    int recv_seq;
    int last_ack;
    int last_ack_times;
    int recv_ack;
    int send_seq;
    int send_flag;
    int seq = 2568;
    int newport = 0;
    int recv_lock = 0;
    int eof = 0;
    int send_exist = 0;
    int buf_to_send = 0;
    int ssth = 0;
    float congestion_size = 1;
    int max_size = 0;
    uint32_t ts;
    FILE* file;
    char* payload;
    char* filename;
    struct rtt_info rttinfo;

    rtt_init(&rttinfo);
    for (i=0;i<count;i++) {
        if (i != index) {
            Close(sockfds[i]);
            continue;
        }
    }

    // todo: file not exist
    // todo: pipeline
    payload = get_payload_ptr(ptr_conn_gram);
    recv_windowsize = *(int*)payload;
    filename = payload+sizeof(int);

    printf("Client [%s:%d] request %s\n", inet_ntoa(ptr_connaddr->sin_addr), ntohs(ptr_connaddr->sin_port), filename);

    file = fopen(filename, "r");
    if (!file) {
        printf("File not exist: %s\n", filename);
        Close(sockfd);
        return;
    }

    fseek(file, 0, SEEK_END);
    filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    local = is_local(addrs[index], ptr_connaddr, 0);

    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
    Setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = ((struct sockaddr_in*)addrs[index]->ifi_addr)->sin_addr.s_addr;
    servaddr.sin_port        = 0;

    Bind(sockfd, (SA *) &servaddr, sizeof(servaddr));

    len = sizeof(servaddr);
    getsockname(sockfd, (SA *) &servaddr, &len);

    if (local == 0) {
        printf("Client [%s:%d] is not LOCAL\n", inet_ntoa(ptr_connaddr->sin_addr), ntohs(ptr_connaddr->sin_port));
    } else if (local == 1) {
        printf("Client [%s:%d] is LOCAL\n", inet_ntoa(ptr_connaddr->sin_addr), ntohs(ptr_connaddr->sin_port));
    } else if (local == 2) {
        printf("Client [%s:%d] is LOOPBACK\n", inet_ntoa(ptr_connaddr->sin_addr), ntohs(ptr_connaddr->sin_port));
    }

    if (local)
        Setsockopt(sockfd, SOL_SOCKET, SO_DONTROUTE, &on, sizeof(on));

    //Connect(sockfd, (SA*)ptr_connaddr, sizeof(struct sockaddr_in));

    // init
    // todo: fail
    sendbuf = (struct datagram*)malloc(windowsize*sizeof(struct datagram));
    memset(sendbuf, 0, windowsize*sizeof(struct datagram));
    sendtv = (uint32_t*)malloc(windowsize*sizeof(uint32_t));
    memset(sendtv, 0, windowsize*sizeof(uint32_t));
    send_exist = 0;
    last_ack = 0;
    last_ack_times = 0;

    newport = (int)ntohs(servaddr.sin_port);
    printf("New port for client [%s:%d] is %d\n", inet_ntoa(ptr_connaddr->sin_addr), ntohs(ptr_connaddr->sin_port), newport);

    get_header(ptr_conn_gram, 0, &recv_seq, 0, &ts);
    make_datagram(&sendbuf[0], SYN | ACK, seq, recv_seq+1, ts, (char*)&newport, sizeof(newport));
    sendtv[0] = rtt_ts(&rttinfo) + rtt_start(&rttinfo);
    send_to(sockfd, &sendbuf[0], ptr_connaddr, sizeof(struct sockaddr_in));
    send_exist++;

    FD_ZERO(&allset);
    FD_ZERO(&rset);
    FD_SET(sockfd, &allset);
    maxfdp1 = sockfd+1;

    for(;;) {
        rset = allset;
        // handle timeout
        // todo: rtt, recv_lock
        ts = rtt_start(&rttinfo);
        struct timeval tv;
        tv.tv_sec = ts/1000;
        tv.tv_usec = (ts % 1000)*1000;

        nready = select(maxfdp1, &rset, NULL, NULL, &tv);
        if (nready < 0) {
            if (errno == EINTR)
                continue;       /* back to for() */
            else
                err_sys("select error");
        } else if (nready == 0) {
            // timeout
            if (recv_lock) {
                ts = rtt_ts(&rttinfo);
                make_datagram(&send_gram, ACK, 0, 0, ts, NULL, 0);
                send_to(sockfd, &send_gram, ptr_connaddr, sizeof(*ptr_connaddr));
                continue;
            }

            // resend
            for (i=0;i<windowsize;i++) {
                if (0 == sendtv[i])
                    continue;

                ts = rtt_ts(&rttinfo);
                if (ts >= sendtv[i]) {
                    set_ts(&sendbuf[i], ts);
                    send_to(sockfd, &sendbuf[i], ptr_connaddr, sizeof(*ptr_connaddr));
                    sendtv[i] = ts + rtt_start(&rttinfo);
                }
            }

            if (rtt_timeout(&rttinfo) < 0) {
                if (!servsockclosed) {
                    Close(sockfds[index]);
                    servsockclosed = 1;
                }

                Close(sockfd);
                fclose(file);

                printf("Too many timeouts for Client [%s:%d]\n", inet_ntoa(ptr_connaddr->sin_addr), ntohs(ptr_connaddr->sin_port));
                return;
            }

            ssth = (int)congestion_size/2;
            if (ssth < 1)
                ssth = 1;
            congestion_size = 1;
            printf("Timeout slow start\n");
            continue;
        }

        if (FD_ISSET(sockfd, &rset)) {
            len = sizeof(cliaddr);
            recv_from(sockfd, &recv_gram, &cliaddr, &len);
            get_header(&recv_gram, 0, 0, &recv_ack, &ts);
            rtt_stop(&rttinfo, rtt_ts(&rttinfo)-ts);

            payload = get_payload_ptr(&recv_gram);
            recv_lock = (*(int*)payload == 0)?1:0; 

            if (!servsockclosed) {
                Close(sockfds[index]);
                servsockclosed = 1;
            }
            
            for (i=0;i<windowsize;i++) {
                get_header(&sendbuf[i], &send_flag, &send_seq, 0, 0);
                if (!send_flag)
                    continue;
                
                if (send_seq < recv_ack) {
                    set_flag(&sendbuf[i], 0);
                    sendtv[i] = 0;
                    send_exist--;
                }
            }

            if ((recv_ack > seq) && eof) {
                Close(sockfd);
                fclose(file);

                printf("Client [%s:%d] end\n", inet_ntoa(ptr_connaddr->sin_addr), ntohs(ptr_connaddr->sin_port));
                return;
            }

            if (last_ack == recv_ack) {
                last_ack_times++;
            } else {
                last_ack = recv_ack;
                last_ack_times=1;
            }

            if (last_ack_times >= 3) {
                last_ack = 0;
                for (i=0;i<windowsize;i++) {
                    get_header(&sendbuf[i], &send_flag, &send_seq, 0, 0);
                    if (!send_flag)
                        continue;

                    if (send_seq == recv_ack)
                        break;
                }
                
                ts = rtt_ts(&rttinfo);
                set_ts(&sendbuf[i], ts);
                send_to(sockfd, &sendbuf[i], ptr_connaddr, sizeof(*ptr_connaddr));
                // todo: congestion
                printf("Fast recvory\n");
                congestion_size = ssth;
                if (congestion_size <= 0)
                    congestion_size = 1;

                continue;
            }

            // todo: congestion
            max_size = windowsize;
            if (recv_windowsize < max_size)
                max_size = recv_windowsize;

            if (congestion_size < ssth) {
                congestion_size++;
                if (congestion_size >= ssth) {
                    printf("Reach slow start threshold\n");
                }
            } else {
                congestion_size += (max_size > 0)? 1.0/max_size:1.0;
            }

            if (congestion_size < max_size)
                max_size = (int)congestion_size;

            if (eof || recv_lock) {
                buf_to_send = 0;
            } else if (send_exist < max_size) {
                buf_to_send = max_size - send_exist;
            } else if (send_exist >= max_size) {
                buf_to_send = 0;
            }

            //printf("buf_to_send %d\n", buf_to_send);
            for (;buf_to_send > 0;buf_to_send--) {
                for (i=0;i<windowsize;i++) {
                    get_header(&sendbuf[i], &send_flag, &send_seq, 0, 0);
                    if (!send_flag)
                        break;
                }

                seq++;
                ts = rtt_ts(&rttinfo);
                make_datagram(&sendbuf[i], DAT, seq, 0, ts, NULL, 0);
                payload = get_payload_ptr(&sendbuf[i]);
                //len = fread(payload+4, BUFFER_SIZE-HEADER_SIZE-4, 1, file);
                len = fread(payload+4, 1, BUFFER_SIZE-HEADER_SIZE-4, file);
                *(int*)payload = len;
                //printf("f %d %s\n", len, payload+4);
                if (ftell(file) >= filesize) {
                    eof = 1;
                    set_fin_flag(&sendbuf[i]);
                }

                sendtv[i] = ts + rtt_start(&rttinfo);
                send_to(sockfd, &sendbuf[i], ptr_connaddr, sizeof(*ptr_connaddr));
                send_exist++;

                if (eof)
                    break;
            }
        }
    }
}

int main(int argc, char** argv) {
    struct datagram conn_gram;
    struct ifi_info *ifihead, *ifi;
    struct sockaddr_in servaddr;
    struct sockaddr_in cliaddr;
    fd_set rset, allset;
    socklen_t len;
    ssize_t n;
    int nready;
    int on = 1;
    int maxfdp1 = 0;
    int i;
    int send_flag;
    pid_t pid;

    ifihead = get_ifi_head();
    printf("Server IP Addresses:\n");
    print_ifi_info(ifihead);

    readfile();

    printf("Listening port %d\n", port);

    count = 0;
    for (ifi=ifihead;ifi!=NULL;ifi=ifi->ifi_next) {
	if (!(ifi->ifi_flags & IFF_UP))
		continue;

        addrs[count] = ifi;
        sockfds[count] = Socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfds[count] > maxfdp1)
            maxfdp1 = sockfds[count];

        Setsockopt(sockfds[count], SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        memcpy(&servaddr, ifi->ifi_addr, sizeof(servaddr));
        servaddr.sin_port = htons(port);
        Bind(sockfds[count], (SA *) &servaddr, sizeof(servaddr));

        count++;
        if (count >= MAX_IFI_COUNT)
            break;
    }

    FD_ZERO(&allset);
    FD_ZERO(&rset);
    for (i=0;i<count;i++) {
        FD_SET(sockfds[i], &allset);
    }
    maxfdp1 = maxfdp1+1;

    signal(SIGCHLD, sigchld);
    for(;;) {
        rset = allset;
        if ((nready = select(maxfdp1, &rset, NULL, NULL, NULL)) < 0) {
            if (errno == EINTR)
                continue;
            else
                err_sys("select error");
        }

        for (i=0;i<count;i++) {
            if (FD_ISSET(sockfds[i], &rset)) {
                len = sizeof(cliaddr);
                n = recv_from(sockfds[i], &conn_gram, &cliaddr, &len);
                send_flag = get_flag(&conn_gram);
                if (!n || (send_flag & RETRY)) {
                    // client timeout, don't fork new child
                    continue;
                }

                printf("Client [%s:%d] connected\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));

                pid = fork();
                if (!pid) {
                    child(i, &cliaddr, &conn_gram);
                    // exit
                    return 0;
                }
            }
        }
    }

    free_ifi_info_plus(ifihead);

    return 0;
}
