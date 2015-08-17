#include <stdio.h>
#include <math.h>
#include "get_ifi.h"
#include "data.h"
#include "unprtt.h"

static char server[MAXLINE];
static char filename[MAXLINE];
static int port = 6825;
static int windowsize = 25;
static int seed = 6825;
static float p = 0.25;
static int u = 25;

static struct ifi_info *addrs[MAX_IFI_COUNT];
static int count = 0;

static struct datagram* recvbuf = NULL;
static int seq = 6825;
static int ack = 0;
static int consume = 0;

static pthread_mutex_t mutex;
static pthread_cond_t cond;

float random_small() {
    float tmp;
    tmp = (float)rand();
    tmp /= (float)RAND_MAX;

    return tmp;
}

int readfile() {
    FILE* file;
    int fd;
    ssize_t len;
    char tmp[MAXLINE];

    file = fopen("client.in", "r");
    if (!file)
        return 0;
    fd = fileno(file);
    len = Readline(fd, server, MAXLINE);
    if (!len)
        goto error;
    server[len-1] = 0;
    len = Readline(fd, tmp, MAXLINE);
    if (!len)
        goto error;
    tmp[len-1] = 0;
    port = atoi(tmp);
    len = Readline(fd, filename, MAXLINE);
    if (!len)
        goto error;
    filename[len-1] = 0;
    len = Readline(fd, tmp, MAXLINE);
    if (!len)
        goto error;
    tmp[len-1] = 0;
    windowsize = atoi(tmp);
    if (windowsize < 1)
        windowsize = 1;

    len = Readline(fd, tmp, MAXLINE);
    if (!len)
        goto error;
    tmp[len-1] = 0;
    seed = atoi(tmp);
    len = Readline(fd, tmp, MAXLINE);
    if (!len)
        goto error;
    tmp[len-1] = 0;
    p = atof(tmp);
    len = Readline(fd, tmp, MAXLINE);
    if (!len)
        goto error;
    tmp[len-1] = 0;
    u = atoi(tmp);

    fclose(file);
    return 1;
error:
    fclose(file);
    return 0;
}

static void*
tdoit(void *arg)
{
    char* buf;
    int recv_flag;
    int recv_seq;
    int i;
    int finish = 0;
    int len;
    int readed;
    float tmp;
    useconds_t usec;

    for (;;) {
        float tmp = random_small();
        tmp = -log(tmp);
        usec = (useconds_t)u*tmp*1000;
        //printf("sleep %d %lf\n", u, tmp);
        usleep(usec);

        readed = 0;
        Pthread_mutex_lock(&mutex);
        for (;;) {
            for (i=0;i<windowsize;i++) {
                get_header(&recvbuf[i], &recv_flag, &recv_seq, 0, 0);
                if (0 == recv_flag)
                    continue;

                if (recv_seq == (consume + 1))
                    break;
            }

            if (i >= windowsize)
                break;

            // printf seq
            readed = 1;
            buf = get_payload_ptr(&recvbuf[i]);
            len = *(int*)buf;
            //printf("len %d\n", len);
            if (len) {
                //printf("[read]\n");
                printf("[read] seq: %d\n%s\n", recv_seq, buf+4);
            }

            if (recv_flag & FIN) {
                finish = 1;
            }

            set_flag(&recvbuf[i], 0);
            consume++;
        }
        Pthread_mutex_unlock(&mutex);

        if (readed) {
            pthread_cond_signal(&cond);
        }

        if (finish)
            return NULL;
    }

    return NULL;
}

void send_to(int sockfd, struct datagram* ptr_gram,
               const struct sockaddr_in *dest_addr, socklen_t addrlen) {
    char buf[MAXLINE];
    float tmp;

    get_str(ptr_gram, buf, MAXLINE);

    tmp = random_small();
    if (tmp >= p) {
        Sendto(sockfd, ptr_gram->buf, BUFFER_SIZE, 0, (SA*)dest_addr, addrlen);

        printf("Send to server[%s:%d] %s\n", inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port), buf);
    } else {
        printf("[drop] Send to server[%s:%d] %s\n", inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port), buf);
    }
}

ssize_t recv_from(int sockfd, struct datagram* ptr_gram,
             const struct sockaddr_in *dest_addr, socklen_t* addrlen) {
    char buf[MAXLINE];

    Recvfrom(sockfd, ptr_gram->buf, BUFFER_SIZE, 0, (SA*)dest_addr, addrlen);

    get_str(ptr_gram, buf, MAXLINE);

    printf("Receive from server[%s:%d] %s\n", inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port), buf);
}

int main(int argc, char** argv) {
    struct datagram gram;
    struct sockaddr_in servaddr, cliaddr;
    struct ifi_info *ifihead, *ifi;
    unsigned int subnet = 0;
    unsigned long tmp = 0;
    int sockfd;
    //int filepos = -1;
    int pos;
    int len;
    int index = -1;
    int loopindex = -1;
    int local = 0;
    int nready;
    int on = 1;
    int i;
    char buf[BUFFER_SIZE];
    fd_set rset, allset;
    int maxfdp1 = 0;
    int newport;
    int timewait = 0;
    int free;
    int recv_seq;
    int tmp_seq;
    int recv_flag;
    int recv_free;
	pthread_t tid;
    struct rtt_info rttinfo;
    uint32_t send_ts;
    uint32_t ts;
    char* payload;

    ifihead = get_ifi_head();

    printf("Client IP Addresses:\n");
    print_ifi_info(ifihead);

    count = 0;
    for (ifi=ifihead;ifi!=NULL;ifi=ifi->ifi_next) {
	//if (ifi->ifi_flags & IFF_MULTICAST)
	//	continue;
        if (ifi->ifi_flags & IFF_LOOPBACK) {
            loopindex = count;
        }

        addrs[count] = ifi;

        count++;
        if (count >= MAX_IFI_COUNT)
            break;
    }

    readfile();

    // init
    srand(seed);
    recvbuf = (struct datagram*)malloc(windowsize*sizeof(struct datagram));
    bzero(recvbuf, windowsize*sizeof(struct datagram));
    Pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    // todo: print
    printf("Connect server: %s\n", server);

    inet_aton(server, &servaddr.sin_addr);
    servaddr.sin_family      = AF_INET;
    servaddr.sin_port        = htons(port);

    for (i=0;i<count;i++) {
        local = is_local(addrs[i], &servaddr, &tmp);
        if (local == 2) {
            index = loopindex;
            break;
        } else if (local == 1) {
            if (tmp > subnet) {
                subnet = tmp;
                index = i;
            }
        } else if (local == 0 && index < 0) {
            if (!(addrs[i]->ifi_flags & IFF_LOOPBACK)) {
                index = i;
            }
        }
    }

    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
    Setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    bzero(&cliaddr, sizeof(cliaddr));
    cliaddr.sin_family      = AF_INET;
    cliaddr.sin_addr.s_addr = ((struct sockaddr_in*)addrs[index]->ifi_addr)->sin_addr.s_addr;
    cliaddr.sin_port        = 0;

    Bind(sockfd, (SA *) &cliaddr, sizeof(cliaddr));

    len = sizeof(cliaddr);
    getsockname(sockfd, (SA *) &cliaddr, &len);

    if (local)
        Setsockopt(sockfd, SOL_SOCKET, SO_DONTROUTE, &on, sizeof(on));

    if (local == 2) {
        memcpy(&servaddr.sin_addr, &cliaddr.sin_addr, sizeof(servaddr.sin_addr));
    }

    if (local == 0) {
        printf("Server [%s:%d] is not LOCAL\n", inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port));
    } else if (local == 1) {
        printf("Server [%s:%d] is LOCAL\n", inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port));
    } else if (local == 2) {
        printf("Server [%s:%d] is LOOPBACK\n", inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port));
    }

    //Connect(sockfd, (SA *) &servaddr, sizeof(servaddr));

    // first send
    bzero(buf, BUFFER_SIZE);
    *(int*)buf = windowsize;
    strcpy(buf+4, filename);

    ts = rtt_ts(&rttinfo);
    make_datagram(&gram, SYN, seq, ack, ts, buf, strlen(filename)+4);
    send_to(sockfd, &gram, &servaddr, sizeof(servaddr));

    FD_ZERO(&allset);
    FD_ZERO(&rset);
    FD_SET(sockfd, &allset);
    maxfdp1 = sockfd+1;

    for(;;) {
        rset = allset;
        // todo: time wait timeout
        // todo: pipeline
        ts = rtt_start(&rttinfo);
        if (timewait)
            ts *= 2;

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

            // finish
            if (timewait) {
                break;
            }

            printf("Timeout\n");

            // retry
            set_retry_flag(&gram);
            
            ts = rtt_ts(&rttinfo);
            set_ts(&gram, ts);
            send_to(sockfd, &gram, &servaddr, sizeof(servaddr));

            if (rtt_timeout(&rttinfo) < 0) {
                printf("Too many timeouts\n");
                Close(sockfd);
                free_ifi_head(ifihead);
                return 0;
            }

            continue;
        }

        if (FD_ISSET(sockfd, &rset)) {
            if (timewait) {
                // server timeout
                len = sizeof(servaddr);
                recv_from(sockfd, &recvbuf[0], &servaddr, &len);

                send_to(sockfd, &gram, &servaddr, sizeof(servaddr));
                continue;
            }

            if (ack <= 0) {
                free = 0;
            } else {
                for (;;) {
                    Pthread_mutex_lock(&mutex);
                    for (free=0;free<windowsize;free++) {
                        recv_flag = get_flag(&recvbuf[free]);
                        if (0 == recv_flag)
                            break;
                    }

                    if (free >= windowsize) {
                        printf("Receiving window is locked\n");
                        pthread_cond_wait(&cond, &mutex);
                        printf("Receiving window is unlocked\n");
                        send_to(sockfd, &gram, &servaddr, sizeof(servaddr));
                    }
                    Pthread_mutex_unlock(&mutex);

                    if (free < windowsize)
                        break;
                }
            }

            if (ack == 0) {
                len = sizeof(servaddr);
                recv_from(sockfd, &recvbuf[free], &servaddr, &len);
                get_header(&recvbuf[free], 0, &ack, 0, &ts);
                get_payload(&recvbuf[free], (char*)&newport, sizeof(newport));
                rtt_stop(&rttinfo, rtt_ts(&rttinfo)-ts);
                consume = ack;
                set_flag(&recvbuf[free], 0);

                servaddr.sin_port = htons(newport);
                //Connect(sockfd, (SA*)&servaddr, sizeof(servaddr));

                seq++;
                ts = rtt_ts(&rttinfo);
                make_datagram(&gram, ACK, seq, ack+1, ts, (char*)&windowsize, sizeof(windowsize));
                send_to(sockfd, &gram, &servaddr, sizeof(servaddr));

                Pthread_create(&tid, NULL, &tdoit, NULL);
                continue;
            }

            Pthread_mutex_lock(&mutex);
            len = sizeof(servaddr);
            recv_from(sockfd, &recvbuf[free], &servaddr, &len);
            for (;;) {
                for (i=0;i<windowsize;i++) {
                    get_header(&recvbuf[i], &recv_flag, &recv_seq, 0, &ts);
                    if (recv_flag == 0)
                        continue;
                    if (recv_flag & ACK) {
                        set_flag(&recvbuf[i], 0);
                        continue;
                    }

                    if (recv_seq == (ack+1))
                        break;
                }

                if (i >= windowsize)
                    break;

                send_ts = ts;
                ack++;
                if (recv_flag & FIN) {
                    printf("Enter time wait state.\n");
                    timewait = 1;
                }
            }

            recv_free = 0;
            for (i=0;i<windowsize;i++) {
                get_header(&recvbuf[i], &recv_flag, &recv_seq, 0, 0);
                if (recv_flag == 0)
                    recv_free++;
            }

            seq++;
            make_datagram(&gram, ACK, seq, ack+1, send_ts, (char*)&recv_free, sizeof(recv_free));
            send_to(sockfd, &gram, &servaddr, sizeof(servaddr));

            if (!recv_free) {
                printf("Receiving window is locked\n");
                pthread_cond_wait(&cond, &mutex);
                printf("Receiving window is unlocked\n");

                recv_free = 0;
                for (i=0;i<windowsize;i++) {
                    get_header(&recvbuf[i], &recv_flag, &recv_seq, 0, 0);
                    if (recv_flag == 0)
                        recv_free++;
                }

                payload = get_payload_ptr(&gram);
                *(int*)payload = recv_free;
                send_to(sockfd, &gram, &servaddr, sizeof(servaddr));
            }
            Pthread_mutex_unlock(&mutex);
        }
    }

    Pthread_join(tid, NULL);
    
    printf("Transmission is finished\n");

    Close(sockfd);
    free_ifi_head(ifihead);

    return 0;
}
