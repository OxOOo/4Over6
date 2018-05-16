//
// Created by chenyu on 2018/5/15.
//

#include "common.h"
#include <cstdio>

const char* READ_IP(const char* ptr, uint8_t ip[4])
{
    int ip_int[4];
    sscanf(ptr, "%d.%d.%d.%d", &ip_int[0], &ip_int[1], &ip_int[2], &ip_int[3]);
    for(int i = 0; i < 4; i ++) ip[i] = (uint8_t)ip_int[i];
    ptr ++;
    while (*ptr && *ptr != ' ') ptr++;
    return ptr;
}
