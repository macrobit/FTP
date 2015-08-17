#include "get_ifi.h"

extern struct ifi_info *Get_ifi_info_plus(int family, int doaliases);
extern        void      free_ifi_info_plus(struct ifi_info *ifihead);

struct ifi_info* get_ifi_head() {
    struct ifi_info *ifihead;
    struct sockaddr *sa;
    u_char  *ptr;
    int     family, doaliases;

    family = AF_INET;
    doaliases = 0;

    ifihead = Get_ifi_info_plus(family, doaliases);

    return ifihead;
}

void free_ifi_head(struct ifi_info* ifihead) {
    free_ifi_info_plus(ifihead);
}

void print_ifi_info(struct ifi_info* ifihead) {
    struct ifi_info* ifi;
	u_char		*ptr;
	struct sockaddr	*ip, *mask;
    struct sockaddr_in subnet;
    int i;

	for (ifi = ifihead; ifi != NULL; ifi = ifi->ifi_next) {
		printf("%s: \n", ifi->ifi_name);
		if (ifi->ifi_index != 0)
			printf("(%d) ", ifi->ifi_index);
#if 1
		printf("<");
/* *INDENT-OFF* */
		if (ifi->ifi_flags & IFF_UP)			printf("UP ");
		if (ifi->ifi_flags & IFF_BROADCAST)		printf("BCAST ");
		if (ifi->ifi_flags & IFF_MULTICAST)		printf("MCAST ");
		if (ifi->ifi_flags & IFF_LOOPBACK)		printf("LOOP ");
		if (ifi->ifi_flags & IFF_POINTOPOINT)	printf("P2P ");
		printf(">\n");
/* *INDENT-ON* */

		if ( (i = ifi->ifi_hlen) > 0) {
			ptr = ifi->ifi_haddr;
			do {
				printf("%s%x", (i == ifi->ifi_hlen) ? "  " : ":", *ptr++);
			} while (--i > 0);
			printf("\n");
		}
		if (ifi->ifi_mtu != 0)
			printf("  MTU: %d\n", ifi->ifi_mtu);
#endif
		if ( (ip = ifi->ifi_addr) != NULL)
			printf("  IP addr: %s\n",
						Sock_ntop_host(ip, sizeof(*ip)));

/*=================== cse 533 Assignment 2 modifications ======================*/

		if ( (mask = ifi->ifi_ntmaddr) != NULL)
			printf("  network mask: %s\n",
						Sock_ntop_host(mask, sizeof(*mask)));

        
        if (ip && mask) {
            memcpy(&subnet, ip, sizeof(subnet));
            
            ((struct sockaddr_in*)&subnet)->sin_addr.s_addr &= ((struct sockaddr_in*)mask)->sin_addr.s_addr;
			printf("  subnet address: %s\n",
						Sock_ntop_host((struct sockaddr*)&subnet, sizeof(subnet)));
        }
/*=============================================================================*/
#if 0
		if ( (sa = ifi->ifi_brdaddr) != NULL)
			printf("  broadcast addr: %s\n",
						Sock_ntop_host(sa, sizeof(*sa)));
		if ( (sa = ifi->ifi_dstaddr) != NULL)
			printf("  destination addr: %s\n",
						Sock_ntop_host(sa, sizeof(*sa)));
#endif
	}
}

int is_local(struct ifi_info* ifi, struct sockaddr_in* cliaddr, unsigned long* subnet) {
    struct sockaddr_in* ip;
    struct sockaddr_in* netmask;
    unsigned long subnet_src;
    unsigned long subnet_dst;

    ip = (struct sockaddr_in*)ifi->ifi_addr;
    if (ip->sin_addr.s_addr == cliaddr->sin_addr.s_addr) {
        return 2;
    }

    netmask = (struct sockaddr_in*)ifi->ifi_ntmaddr;

    subnet_src = ip->sin_addr.s_addr;
    subnet_src &= netmask->sin_addr.s_addr;

    subnet_dst = cliaddr->sin_addr.s_addr;
    subnet_dst &= netmask->sin_addr.s_addr;
    
    if (subnet_src == subnet_dst) {
        if (subnet)
            *subnet = ntohl(subnet_src);

        return 1;
    }

    return 0;
}
