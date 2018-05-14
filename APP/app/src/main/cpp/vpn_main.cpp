//
// Created by chenyu on 2018/5/15.
//

#include "vpn_main.h"

static int connectServer(const std::string hostname, int port) {
    int ret = 0;
    struct sockaddr_in6 serv_addr;
    struct hostent *server;

    int socketFd = socket(AF_INET6, SOCK_STREAM, 0);
    if (socketFd < 0) return socketFd;

    int flags = fcntl(socketFd, F_GETFL, 0);
    if (flags == -1) return 0;
    flags |= O_NONBLOCK;
    if (fcntl(socketFd, F_SETFL, flags)) return 0;

    server = gethostbyname2(hostName, AF_INET6);
    if (server == NULL)
        return -1;

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin6_flowinfo = 0;
    serv_addr.sin6_family = AF_INET6;
    memmove((char *) &serv_addr.sin6_addr.s6_addr, (char *) server->h_addr, server->h_length);
    serv_addr.sin6_port = htons((unsigned short) port);

    // TODO: Connect is still blocking.
    ret = connect(socketFd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    if (ret < 0 && errno != EINPROGRESS) return ret;
    return socketFd;
}

int vpn_main(const std::string hostname, int port, int commandReadFd, int responseWriteFd)
{
    return 0;
}