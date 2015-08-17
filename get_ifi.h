#ifndef GET_IFI_H
#define GET_IFI_H

#include "unpifiplus.h"

#define MAX_IFI_COUNT 32

struct ifi_info* get_ifi_head();
void free_ifi_head(struct ifi_info*);
int count_ifi_info(struct ifi_info* ifihead);
void print_ifi_info(struct ifi_info* ifihead);
int is_local(struct ifi_info* ifi, struct sockaddr_in* cliaddr, unsigned long* subnet);

#endif
