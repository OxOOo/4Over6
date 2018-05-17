#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <bits/ioctls.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <errno.h>

// multi-thread
#include <pthread.h>


/// constant definitions

#define SERVER_PORT         8192
#define EPOLL_MAX_EVENTS    10
#define POOL_SIZE           128
#define PACKET_MAX_SIZE     2048
#define BUF_SIZE            65536
#define BUF_SIZE_SMALL      128
#define TIMEOUT_INTERVAL    20
#define MAX_FDS             128

const int8_t    TYPE_IP_REQUEST     = 100;
const int8_t    TYPE_IP_RESPONSE    = 101;
const int8_t    TYPE_INET_REQUEST   = 102;
const int8_t    TYPE_INET_RESPONSE  = 103;
const int8_t    TYPE_KEEPALIVE      = 104;


/// struct definitions

// message between server and client
typedef struct message {
    int length;
    char type;
    char data[];
} __attribute__((packed)) message;

// user info list item
typedef struct user_info {
    int fd;
    int count;
    uint64_t secs;
    struct in_addr v4addr;
    struct in6_addr v6addr;
    struct user_info *pNext;
} user_info;

// IP address pool item
typedef struct ip_addr_entry {
    char addr[32];
    int status;
} ip_addr_entry;

// arguments for tun thread
typedef struct args_tun {
    int fd;
    char *buf;
} args_tun;

// arguments for keep-alive thread
typedef struct args_keepalive {
    int epoll_fd;
} args_keepalive;

// buffer
typedef struct buffer_entry {
    char *buf;
    int used;
} buffer_entry;

/// global variables

// user info table
user_info *infotable = NULL;

// address pool
ip_addr_entry addrpool[POOL_SIZE];

// buffer pool
buffer_entry bufs[MAX_FDS];

/// system operations

// set socket as non-blocking
int set_nonblocking(int fd) {
    int flags;
    if ((flags = fcntl(fd, F_GETFL, 0)) < 0)
        return -1;
    if ((flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK)) < 0)
        return -1;
    return 0;
}

// server socket initialization
int server_init() {
    int fd;
    struct sockaddr_in6 serveraddr;

    // open server socket
    if ((fd = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
        printf("Error: open server socket failed\n");
        return -1;
    }

    // enable address reuse
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&on, sizeof(on)) < 0) {
        printf("Error: enable address use failed\n");
        return -1;
    }

    // bind server address
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin6_family = AF_INET6;
    serveraddr.sin6_port = htons(SERVER_PORT);
    serveraddr.sin6_addr = in6addr_any;
    if (bind(fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        printf("Error: bind server socket failed\n");
        return -1;
    }

    // listen on server socket
    if (listen(fd, 20) < 0) {
        printf("Error: listen on socket failed\n");
        return -1;
    }

    return fd;
}

// /dev/net/tun initialization
int tun_init() {
    int fd;

    // open tun
    if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
        printf("Error: open /dev/net/tun failed\n");
        return -1;
    }

    // issue name change request
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    strncpy(ifr.ifr_name, "over6", IFNAMSIZ);

    if (ioctl(fd, TUNSETIFF, (void *)&ifr) == -1) {
        printf("Error: set tun interface name failed\n");
        return -1;
    }

    // set as non-blocking
    // if (set_nonblocking(fd) < 0) {
    //     printf("Error: failed to set tun fd as non-blocking\n");
    //     return -1;
    // }

    system("ifconfig over6 13.8.0.1 netmask 255.255.255.0 up");
    system("iptables -t nat -A POSTROUTING -s 13.8.0.0/24 -j MASQUERADE");

    return fd;
}

// get DNS address
int get_dns(char *addrdns) {
    // system call
    system("cat /etc/resolv.conf | grep -i nameserver | cut -c 12-30 > dns.txt");

    // retrieve DNS address from dns.txt
    FILE *fp = fopen("dns.txt", "r");
    if (fp == NULL) {
        printf("Error: can not open dns.txt\n");
        return -1;
    }

    fscanf(fp, "%s", addrdns);
    fclose(fp);
    fp = NULL;

    return 0;
}

// add socket to epoll
int epoll_add_fd(int epoll_fd, int fd, int events) {
    // set input socket to non-blocking mode
    if (set_nonblocking(fd) < 0) {
        printf("Error: failed to set socket fd %d as non-blocking\n", fd);
        return -1;
    }

    // add socket to epoll
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = events;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        printf("Error: failed to add fd %d to epoll\n", fd);
        return -1;
    }
    return 0;
}

// remove fd from epoll
int epoll_remove_fd(int epoll_fd, int fd) {
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        printf("Error: failed to remove fd %d from epoll\n", fd);
        return -1;
    }
    return 0;
}

// allocate ip address from pool
int allocate_ip_addr() {
    int i;
    for (i = 2; i < POOL_SIZE; i++)
        if (addrpool[i].status == 0) {
            addrpool[i].status = 1;
            return i;
        }
    return -1;
}

// deallocate ip address from pool
int deallocate_ip_addr(const char *addr) {
    int i;
    for (i = 2; i < POOL_SIZE; i++)
        if (!strcmp(addr, addrpool[i].addr)) {
            if (addrpool[i].status == 0) {
                printf("Error: deallocate non-allocated ip address %s\n", addr);
                return -1;
            }

            return i;
        }

    printf("Error: deallocate non-existing ip address %s\n", addr);
    return -1;
}

// read data from input fd
int read_data(int fd, char *buf, int *used) {
    int tot = 0;
    int ret;
    while (tot + PACKET_MAX_SIZE < BUF_SIZE) {
        if ((ret = read(fd, buf + *used, PACKET_MAX_SIZE)) <= 0)
            break;
        *used += ret;
        tot += ret;
    }
    return tot ? tot : ret;
}

// write data to output fd
int write_data(int fd, char *buf, int size) {
    int offset = 0;
    while(offset < size) {
        int ret = write(fd, buf + offset, size - offset);
        if (ret < 0) {
            printf("Error: write data error %d:%s\n", errno, strerror(errno));
            return ret;
        }
        offset += ret;
    }
    return offset;
}

// add user info entry into table
int add_user_info(int fd, int ip) {
    user_info *ptr = NULL;
    ptr = (user_info *)malloc(sizeof(user_info));
    if (ptr == NULL)
        return -1;

    ptr->fd = fd;
    ptr->count = TIMEOUT_INTERVAL;
    ptr->secs = time(0);
    ptr->pNext = infotable;
    inet_pton(AF_INET, addrpool[ip].addr, (void *)&ptr->v4addr);

    struct sockaddr_in6 clientaddr;
    socklen_t addrsize = sizeof(clientaddr);
    getpeername(fd, (struct sockaddr *)&clientaddr, &addrsize);
    ptr->v6addr = clientaddr.sin6_addr;

    infotable = ptr;

    return 0;
}

// search for user info entry given fd
user_info *search_user_info(int fd) {
    user_info *ptr = infotable;
    while (ptr != NULL && ptr->fd != fd)
        ptr = ptr->pNext;

    return ptr;
}

// search for user info entry given ipv4 address
user_info *search_user_info_addr(uint32_t addr) {
    user_info *ptr = infotable;
    while (ptr != NULL && ptr->v4addr.s_addr != addr)
        ptr = ptr->pNext;

    return ptr;
}

// delete an user info entry
int delete_user_info(user_info *entry) {
    user_info *ptr = infotable;
    if (entry == infotable) {
        infotable = entry->pNext;
        entry->pNext = NULL;
        free(infotable);
        return 0;
    }

    while (ptr != NULL && ptr->pNext != entry)
        ptr = ptr->pNext;

    if (ptr == NULL) {
        printf("Error: user info entry to be deleted does not exist\n");
        return -1;
    }

    ptr->pNext = entry->pNext;
    entry->pNext = NULL;
    free(entry);

    return 0;
}

// deallocate user info table
void free_user_info(user_info *ptr) {
    user_info *nextptr = NULL;
    while (ptr != NULL) {
        nextptr = ptr->pNext;
        free(ptr);
        ptr = nextptr;
    }
}

// process incoming data from client
int process_client_data(int fd, int tun_fd, char *buf, int *used) {
    char bufreply[BUF_SIZE_SMALL];

    for (;;) {
        message *msg = (message *)buf;
        int msglen = 0;

        // incomplete package
        if (*used < 4)
            return 0;
        msglen = msg->length;
        if (*used < msglen)
            return 0;

        // process data
        if (msg->type == TYPE_IP_REQUEST) {
            // allocate ip address
            int ip;
            if ((ip = allocate_ip_addr()) < 0) {
                printf("Error: ip address pool is full\n");
                return -1;
            }

            // send ip response
            sprintf(bufreply, "%s 0.0.0.0 166.111.8.28 166.111.8.29 8.8.8.8", addrpool[ip].addr);
            int buflen = strlen(bufreply);

            message reply;
            reply.type = TYPE_IP_RESPONSE;
            reply.length = buflen + sizeof(message);
            if (write(fd, &reply, sizeof(reply)) < (int)sizeof(reply)) {
                printf("Error: send reply header failed\n");
                return -1;
            }
            if (write(fd, bufreply, buflen) < buflen) {
                printf("Error: send reply data failed\n");
                return -1;
            }

            // add to user info table
            if (add_user_info(fd, ip) < 0) {
                printf("Error: add user info table entry failed\n");
                return -1;
            }
        }
        else if (msg->type == TYPE_INET_REQUEST) {
            // send data to tun
            int datalen = msglen - sizeof(message);
            if (write_data(tun_fd, msg->data, datalen) < datalen) {
                printf("Error: send data to tun failed\n");
                return -1;
            }
        }
        else if (msg->type == TYPE_KEEPALIVE) {
            printf("client keep-alive\n");

            user_info *entry = NULL;
            if ((entry = search_user_info(fd)) == NULL) {
                printf("Error: can not locate the source of keep-alive package\n");
                return -1;
            }
            
            // set new keepalive timestamp
            entry->secs = time(0);
        }
        else {
            printf("Warning: unknown type of data from client fd %d\n", fd);
        }

        printf("Package client - length: %d\n", msglen);
        memmove(buf, buf + msglen, *used - msglen);
        *used -= msglen;
    }
}

// thread function for tun
void *thread_tun(void *args) {
    args_tun *argsptr = (args_tun *)args;
    int tun_fd = argsptr->fd;
    char *buf = argsptr->buf;
    int client_fd = -1;

    for (;;) {
        int nread;
        // read data from tun
        if ((nread = read(tun_fd, buf, BUF_SIZE)) < 0) {
            printf("Error: can not read from tun\n");
            // pthread_exit(NULL);
        }

        if (nread < (int)sizeof(struct iphdr))
            continue;

        message reply;
        reply.type = TYPE_INET_RESPONSE;
        reply.length = nread + sizeof(reply);

        // extract ip address from ip header
        struct iphdr *hdr = (struct iphdr *)buf;
        user_info *entry = search_user_info_addr(hdr->daddr);
        if (entry == NULL)
            continue;

        client_fd = entry->fd;

        int ret;
        if ((ret = write(client_fd, &reply, sizeof(reply))) < (int)sizeof(reply)) {
            printf("Error: send reply header failed\n");
            // pthread_exit(NULL);
        }
        if ((ret = write(client_fd, buf, nread)) < 0) {
            printf("Error: send reply data failed\n");
            // pthread_exit(NULL);
        }

        printf("Package tunnel - length: %lu\n", nread + sizeof(message));
    }

    return NULL;
}

// thread function for keepalive
void *thread_keepalive(void *args) {
    args_keepalive *argsptr = (args_keepalive *)args;
    int epoll_fd = argsptr->epoll_fd;
    int client_fd = -1;
    char addrpeer[INET_ADDRSTRLEN];

    for (;;) {
        // wait for 1 second
        sleep(1);

        // check each client
        user_info *ptr = infotable;
        while (ptr != NULL) {
            // requires keepalive packet
            if (--ptr->count == 0) {
                client_fd = ptr->fd;

                // send keepalive packet
                message reply;
                reply.type = TYPE_KEEPALIVE;
                reply.length = sizeof(message);

                if (write(client_fd, &reply, sizeof(reply)) < (int)sizeof(reply)) {
                    printf("Error: send keepalive package failed\n");
                    pthread_exit(NULL);
                }

                ptr->count = 20;
            }

            user_info *oldptr = ptr;
            ptr = ptr->pNext;

            // timeout for client keepalive package
            if (time(0) - oldptr->secs > (uint64_t)60) {
                printf("client timeout\n");

                inet_ntop(AF_INET, &oldptr->v4addr, addrpeer, sizeof(addrpeer));
                deallocate_ip_addr(addrpeer);
                delete_user_info(oldptr);

                // erase client fd from epoll
                if (epoll_remove_fd(epoll_fd, client_fd) < 0) {
                    printf("Error: failed to remove client fd %d from epoll\n", client_fd);
                    pthread_exit(NULL);
                }

                // release client fd
                close(client_fd);
                client_fd = -1;
            }
        }
    }

    return NULL;
}

int main(int argc, char **argv) {
    // initialize buffers
    char *buftun = NULL;

    // initialize server socket
    int server_fd;
    if ((server_fd = server_init()) == -1)
        goto clean;
    printf("server_fd = %d\n", server_fd);

    // initialize tun
    int tun_fd;
    if ((tun_fd = tun_init()) == -1)
        goto clean;
    printf("tun_fd = %d\n", tun_fd);

    // initialize epoll
    int epoll_fd;
    if ((epoll_fd = epoll_create(EPOLL_MAX_EVENTS)) < 0) {
        printf("Error: create epoll fd failed\n");
        goto clean;
    }

    // add server and tun sockets to epoll
    int epoll_events = EPOLLIN;
    if (epoll_add_fd(epoll_fd, server_fd, epoll_events) < 0) {
        printf("Error: failed to add server socket to epoll\n");
        goto clean;
    }
    // if (epoll_add_fd(epoll_fd, tun_fd, epoll_events) < 0) {
    //     printf("Error: failed to add tun to epoll\n");
    //     goto clean;
    // }

    // initialize address pool
    for (int i = 0; i < POOL_SIZE; i++) {
        sprintf(addrpool[i].addr, "13.8.0.%d", i);
        addrpool[i].status = 0;
    }

    // initialize buffer pool
    for (int i = 0; i < MAX_FDS; i++) {
        bufs[i].buf = malloc(BUF_SIZE);
        bufs[i].used = 0;
    }

    // acquire DNS address
    char addrdns[INET_ADDRSTRLEN];
    get_dns(addrdns);
    printf("DNS address: %s\n", addrdns);

    // allocate buffers
    buftun = malloc(BUF_SIZE);
    if (buftun == NULL) {
        printf("Error: allocate tun buffer failed\n");
        goto clean;
    }

    // create tun thread (TODO: thread cancel?)
    pthread_t tun;
    args_tun argstun;
    argstun.fd = tun_fd;
    argstun.buf = buftun;
    pthread_create(&tun, NULL, thread_tun, (void *)&argstun);

    // create keep-alive thread
    pthread_t keepalive;
    args_keepalive argskeepalive;
    argskeepalive.epoll_fd = epoll_fd;
    pthread_create(&keepalive, NULL, thread_keepalive, (void *)&argskeepalive);

    // event loop
    char addrpeer[INET_ADDRSTRLEN];
    char addrpeer6[INET6_ADDRSTRLEN];
    struct epoll_event events[EPOLL_MAX_EVENTS];
    int client_fd;

    for (;;) {
        // wait for all epoll events
        int num_events;
        if ((num_events = epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, 20)) < 0) {
            printf("Error: failed to wait for epoll events\n");
            goto clean;
        }

        // process each event
        int i;
        for (i = 0; i < num_events; i++) {
            // exception handling
            if (!(events[i].events & epoll_events)) {
                printf("Warning: unknown event\n");
                continue;
            }

            // event from server socket
            if (events[i].data.fd == server_fd) {
                struct sockaddr_in6 clientaddr;
                memset(&clientaddr, 0, sizeof(clientaddr));
                socklen_t addrsize = sizeof(clientaddr);

                // accept new connection from a client
                if ((client_fd = accept(server_fd, (struct sockaddr *)&clientaddr, &addrsize)) < 0) {
                    printf("Error: accept connection failed\n");
                    goto clean;
                }

                // allocate buffer
                bufs[client_fd].used = 0;

                // get and display peer info
                getpeername(client_fd, (struct sockaddr *)&clientaddr, &addrsize);
                inet_ntop(AF_INET6, &clientaddr.sin6_addr, addrpeer6, sizeof(addrpeer6));
                printf("New connection - addr: %s\n", addrpeer6);
                printf("New connection - port: %d\n", ntohs(clientaddr.sin6_port));

                // add new client to epoll list
                if (epoll_add_fd(epoll_fd, client_fd, epoll_events) < 0) {
                    printf("Error: failed to add client fd %d to epoll\n", client_fd);
                    goto clean;
                }
            }
            // event from client socket
            else {
                int nread;
                client_fd = events[i].data.fd;
                if (ioctl(client_fd, FIONREAD, &nread) < 0) {
                    printf("Error: ioctl failed on client fd %d\n", client_fd);
                    goto clean;
                }

                // normal connection (TODO: close all open fds)
                int ret;
                if (nread) {
                    if ((ret = read_data(client_fd, bufs[client_fd].buf, &bufs[client_fd].used)) < 0) {
                        printf("Error: can not read data from client fd %d\n", client_fd);
                        goto clean;
                    }
                    if ((ret = process_client_data(client_fd, tun_fd, bufs[client_fd].buf, &bufs[client_fd].used)) < 0) {
                        printf("Error: failed to process data from client fd %d\n", client_fd);
                        goto clean;
                    }
                }
                // client disconnected
                else {
                    printf("Connection ternimated\n");

                    // delete client info and ip address
                    user_info *entry = search_user_info(client_fd);
                    inet_ntop(AF_INET, &entry->v4addr, addrpeer, sizeof(addrpeer));
                    deallocate_ip_addr(addrpeer);
                    delete_user_info(entry);

                    // erase client fd from epoll
                    if (epoll_remove_fd(epoll_fd, client_fd) < 0) {
                        printf("Error: failed to remove client fd %d from epoll\n", client_fd);
                        goto clean;
                    }

                    // release client fd
                    close(client_fd);
                    client_fd = -1;
                }
            }
        }
    }

clean:
    for (int i = 0; i < MAX_FDS; i++) {
        if (bufs[i].buf)
            free(bufs[i].buf);
    }

    if (buftun != NULL) free(buftun);

    free_user_info(infotable);

    if (tun_fd != -1) close(tun_fd);
    if (client_fd != -1) close(client_fd);
    if (server_fd != -1) close(server_fd);

    return 0;
}
