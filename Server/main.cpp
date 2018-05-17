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

/// constant definitions

#define SERVER_PORT         8192
#define MAX_CLIENTS         256 // index 0 and index 255 are invalid
#define MAX_FDS             2048
#define EPOLL_MAX_EVENTS    MAX_FDS
#define HARTBEAT_INTERVAL   20
#define HARTBEAT_TIMEOUT    60
#define BUF_SIZE            65536

#define EPOLL_EVENTS        (EPOLLIN|EPOLLERR|EPOLLHUP)

#define ADDRESS_PREFIX      "13.8.0"
#define NETMASK             "255.255.255.0"
#define DNS1                "166.111.8.28"
#define DNS2                "166.111.8.29"
#define DNS3                "8.8.8.8"

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
} __attribute__((packed)) message;

// user info
typedef struct user_info {
    char addr[32]; // string ip address
    int is_free;

    int fd; // -1 means none
    time_t last_hartbeat_sent_secs;
    time_t last_hartbeat_recved_secs;
    struct in_addr v4addr;
    struct in6_addr v6addr;
} user_info;

// buffer
typedef struct buffer_entry {
    char buf[BUF_SIZE];
    int used;
} buffer_entry;

/// global variables

struct user_info users[MAX_CLIENTS];
struct buffer_entry bufs[MAX_FDS];

/// system operations

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
    if (listen(fd, MAX_FDS) < 0) {
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

    // enable interface and nat
    system("ifconfig over6 " ADDRESS_PREFIX ".1 netmask 255.255.255.0 up");
    system("iptables -t nat -A POSTROUTING -s " ADDRESS_PREFIX ".0/24 -j MASQUERADE");

    return fd;
}

// add socket to epoll
int epoll_add_fd(int epoll_fd, int fd, int events) {
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
int allocate_ip_addr(int client_fd) {
    int i;
    for(i = 0; i < MAX_CLIENTS; i ++)
        if (users[i].is_free) {
            users[i].is_free = 0;

            users[i].fd = client_fd;
            users[i].last_hartbeat_sent_secs = time(0);
            users[i].last_hartbeat_recved_secs = time(0);
            inet_pton(AF_INET, users[i].addr, (void *)&users[i].v4addr);

            struct sockaddr_in6 clientaddr;
            socklen_t addrsize = sizeof(clientaddr);
            getpeername(client_fd, (struct sockaddr *)&clientaddr, &addrsize);
            users[i].v6addr = clientaddr.sin6_addr;
            
            return i;
        }
    return -1;
}

// deallocate ip address from pool
int deallocate_ip_addr(int client_fd) {
    int i;
    for(i = 0; i < MAX_CLIENTS; i ++)
        if (users[i].fd == client_fd && !users[i].is_free) {
            users[i].is_free = 1;
            users[i].fd = -1;
            return i;
        }
    return -1;
}

// search for user info entry given fd
int search_user_info_by_fd(int client_fd) {
    int i;
    for(i = 0; i < MAX_CLIENTS; i ++)
        if (users[i].fd == client_fd && !users[i].is_free) {
            return i;
        }
    return -1;
}

// search for user info entry given ipv4 address
int search_user_info_by_addr(uint32_t addr) {
    int i;
    for(i = 0; i < MAX_CLIENTS; i ++)
        if (users[i].v4addr.s_addr == addr && !users[i].is_free) {
            return i;
        }
    return -1;
}

uint16_t in_cksum(void *addr, int len)
{
    uint32_t sum = 0;
    uint16_t answer = 0;
    uint16_t *w = (uint16_t*)addr;
    int nleft = len;
    /*
    * Our algorithm is simple, using a 32 bit accumulator (sum), we add
    * sequential 16 bit words to it, and at the end, fold back all the
    * carry bits from the top 16 bits into the lower 16 bits.
    */
    while (nleft > 1)
    {
        sum += *w++;
        nleft -= 2;
    }
    /* mop up an odd byte, if necessary */
    if (nleft == 1)
    {
        *(u_char *) (&answer) = *(u_char *) w;
        sum += answer;
    }
    /* add back carry outs from top 16 bits to low 16 bits */
    sum = (sum >> 16) + (sum & 0xffff); /* add hi 16 to low 16 */
    sum += (sum >> 16); /* add carry */
    answer = ~sum; /* truncate to 16 bits */
    return (answer);
}

void free_client_fd(int client_fd, int epoll_fd) {
    if (epoll_remove_fd(epoll_fd, client_fd) < 0) {
        printf("Error: failed to remove client fd %d from epoll\n", client_fd);
        return;
    }
    deallocate_ip_addr(client_fd);
    close(client_fd);
}

int write_all(int fd, void* buf, int size) {
    int offset = 0;
    uint8_t* w = (uint8_t*)buf;
    while(offset < size) {
        int ret;
        if ((ret = write(fd, w+offset, size-offset)) < 0) {
            printf("Error: fail to write data to fd = %d: %s\n", fd, strerror(errno));
            return ret;
        }
        offset += ret;
    }
    return offset;
}

void process_server(int server_fd, int epoll_fd) {
    struct sockaddr_in6 clientaddr;
    memset(&clientaddr, 0, sizeof(clientaddr));
    socklen_t addrsize = sizeof(clientaddr);

    // accept new connection from a client
    int client_fd;
    if ((client_fd = accept(server_fd, (struct sockaddr *)&clientaddr, &addrsize)) < 0) {
        printf("Error: accept connection failed\n");
        return;
    }

    // setup buffer
    bufs[client_fd].used = 0;

    // get and display peer info
    char addrpeer6[BUF_SIZE];
    getpeername(client_fd, (struct sockaddr *)&clientaddr, &addrsize);
    inet_ntop(AF_INET6, &clientaddr.sin6_addr, addrpeer6, sizeof(addrpeer6));
    printf("New connection - addr: %s\n", addrpeer6);
    printf("New connection - port: %d\n", ntohs(clientaddr.sin6_port));

    // add new client to epoll list
    if (epoll_add_fd(epoll_fd, client_fd, EPOLL_EVENTS) < 0) {
        printf("Error: failed to add client fd %d to epoll\n", client_fd);
        return;
    }
}

void process_tun(int tun_fd) {
    char* buf = bufs[tun_fd].buf;
    int& nread = bufs[tun_fd].used;

    int size;
    // read data from tun
    if ((size = read(tun_fd, buf+nread, BUF_SIZE-nread)) < 0) {
        printf("Error: can not read from tun\n");
        return;
    }
    nread += size;

    while(true) {
        // drop wrong ip header
        // while(true) {
        //     if (nread < (int)sizeof(struct iphdr)) break;
            
        //     struct iphdr hdr = *(struct iphdr *)buf;
        //     if (in_cksum(&hdr, sizeof(struct iphdr)) == 0) break; //correct header
        //     memmove(buf, buf+1, nread-1);
        //     nread -= 1;
        //     static int drops = 0;
        //     drops ++;
        //     // printf("drops = %d check = %u\n", drops, in_cksum(&hdr, sizeof(struct iphdr)));
        // }

        if (nread < (int)sizeof(struct iphdr))
            break;
        
        // extract ip address from ip header
        struct iphdr hdr = *(struct iphdr *)buf;
        int iplen = nread;
        // int iplen = hdr.ihl*4+ntohs(hdr.tot_len);
        // printf("process_tun iplen=%d, hdr.ihl=%d, hdr.tot_len=%d\n", iplen, hdr.ihl, hdr.tot_len);
        if (nread < iplen) break;

        do {
            int id = search_user_info_by_addr(hdr.daddr);
            if (id == -1) {
                printf("Warning: can not locate ip packet dest\n");
                continue;
            }
            int client_fd = users[id].fd;

            message reply;
            reply.type = TYPE_INET_RESPONSE;
            reply.length = iplen + sizeof(reply);

            int ret;
            if ((ret = write_all(client_fd, &reply, sizeof(reply))) < (int)sizeof(reply)) {
                printf("Error: send reply header failed\n");
                return;
            }
            if ((ret = write_all(client_fd, buf, iplen)) < 0) {
                printf("Error: send reply data failed\n");
                return;
            }

            printf("Package tunnel - length: %lu\n", iplen + sizeof(message));
        } while(0);

        memmove(buf, buf + iplen, nread - iplen);
        nread -= iplen;
    }
}

void process_client(int client_fd, int tun_fd, int epoll_fd) {
    printf("process_client\n");

    char* buf = bufs[client_fd].buf;
    int& nread = bufs[client_fd].used;
    
    ssize_t size;
    // read data from client
    size = read(client_fd, buf+nread, BUF_SIZE-nread);

    if (size <= 0) {
        if (errno == ECONNRESET || size == 0) {
            printf("Connection ternimated\n");
        } else {
            printf("Error: error at read client:%s\n", strerror(errno));
        }
        free_client_fd(client_fd, epoll_fd);
        return;
    }
    printf("[debug] size = %d\n", size);
    nread += size;

    // process incoming data from client
    while(true) {
        if (nread < sizeof(message)) break;

        message msg = *(message *)buf;
        int msglen = msg.length;
        if (nread < msg.length) break;

        // process data
        if (msg.type == TYPE_IP_REQUEST) {
            // allocate ip address
            int id;
            if ((id = allocate_ip_addr(client_fd)) < 0) {
                printf("Error: ip address pool is full\n");
                return;
            }

            // send ip response
            char bufreply[BUF_SIZE];
            sprintf(bufreply, "%s 0.0.0.0 %s %s %s", users[id].addr, DNS1, DNS2, DNS3);
            int buflen = strlen(bufreply);

            message reply;
            reply.type = TYPE_IP_RESPONSE;
            reply.length = buflen + sizeof(message);
            if (write_all(client_fd, &reply, sizeof(reply)) < (int)sizeof(reply)) {
                printf("Error: send reply header failed\n");
                return;
            }
            if (write_all(client_fd, bufreply, buflen) < buflen) {
                printf("Error: send reply data failed\n");
                return;
            }
        }
        else if (msg.type == TYPE_INET_REQUEST) {
            // send data to tun
            int datalen = msglen - sizeof(message);
            if (write_all(tun_fd, buf+sizeof(message), datalen) < datalen) {
                printf("Error: send data to tun failed\n");
                return;
            }
        }
        else if (msg.type == TYPE_KEEPALIVE) {
            printf("client keep-alive\n");

            int id = search_user_info_by_fd(client_fd);
            users[id].last_hartbeat_recved_secs = time(0);
        }
        else {
            printf("Warning: unknown type of data from client fd %d\n", client_fd);
        }

        printf("Package client - length: %d\n", msglen);
        memmove(buf, buf + msglen, nread - msglen);
        nread -= msglen;
    }
}

void process_hartbeat(int epoll_fd) {
    for(int i = 0; i < MAX_CLIENTS; i ++)
        if (users[i].fd >= 0 && !users[i].is_free) {
            time_t now = time(0);
            if (now - users[i].last_hartbeat_sent_secs > HARTBEAT_INTERVAL) {
                message hartbeat;
                hartbeat.type = TYPE_KEEPALIVE;
                hartbeat.length = sizeof(message);
                if (write_all(users[i].fd, &hartbeat, sizeof(message)) != sizeof(message)) {
                    printf("Error: fail to send hartbeat packet\n");
                }
                users[i].last_hartbeat_sent_secs = now;
            }
            if (now - users[i].last_hartbeat_recved_secs > HARTBEAT_TIMEOUT) {
                free_client_fd(users[i].fd, epoll_fd);
            }
        }
}

int main(int argc, char **argv) {
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
    if (epoll_add_fd(epoll_fd, server_fd, EPOLL_EVENTS) < 0) {
        printf("Error: failed to add server socket to epoll\n");
        goto clean;
    }
    if (epoll_add_fd(epoll_fd, tun_fd, EPOLL_EVENTS) < 0) {
        printf("Error: failed to add tun to epoll\n");
        goto clean;
    }

    // initialize user info pool
    for (int i = 0; i < MAX_CLIENTS; i++) {
        sprintf(users[i].addr, ADDRESS_PREFIX ".%d", i);
        users[i].is_free = 1;
        users[i].fd = -1;
    }
    users[0].is_free = 0; // x.x.x.0 is invalid
    users[1].is_free = 0; // x.x.x.1 is invalid
    users[255].is_free = 0; // x.x.x.255 is invalid

    // initialize buffer pool
    for (int i = 0; i < MAX_FDS; i++) {
        bufs[i].used = 0;
    }

    // event loop
    for (;;) {
        // wait for all epoll events
        int num_events;
        struct epoll_event events[EPOLL_MAX_EVENTS];
        if ((num_events = epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, 1)) < 0) {
            printf("Error: failed to wait for epoll events\n");
            goto clean;
        }

        // process each event
        int i;
        for (i = 0; i < num_events; i++) {
            // exception handling
            // if (!(events[i].events & epoll_events)) {
            //     printf("Warning: unknown event\n");
            //     continue;
            // }

            // event from server socket
            if (events[i].data.fd == server_fd) {
                process_server(server_fd,epoll_fd);
            }
            // event from tun
            else if (events[i].data.fd == tun_fd) {
                process_tun(tun_fd);
            }
            // event from client socket
            else {
                process_client(events[i].data.fd, tun_fd, epoll_fd);
            }
        }

        process_hartbeat(epoll_fd);

        fflush(stdout);
    }

clean:
    if (tun_fd != -1) close(tun_fd);
    if (server_fd != -1) close(server_fd);
    for(int i = 0; i < MAX_CLIENTS; i ++)
        if (users[i].fd >= 0)
            close(users[i].fd);

    return 0;
}
