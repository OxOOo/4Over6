//
// Created by chenyu on 2018/5/15.
//

#include "vpn_main.h"
#include "protocol.h"
#include "common.h"
#include <android/log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <stdlib.h>
#include <vector>

using namespace std;

static int connectServer(const std::string hostname, int port) {
    struct sockaddr_in6 serv_addr;

    int socketFd = socket(AF_INET6, SOCK_STREAM, 0);
    ERROR_CHECK(socketFd, fail2);

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin6_family = AF_INET6;
    serv_addr.sin6_port = htons((unsigned short) port);
    ERROR_CHECK( inet_pton(AF_INET6, hostname.c_str(), &serv_addr.sin6_addr) , fail1);

    ERROR_CHECK( connect(socketFd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) , fail1);

    return socketFd;
    fail1:
    close(socketFd);
    fail2:
    return -1;
}

int vpn_main(const std::string hostname, int port, int commandReadFd, int responseWriteFd)
{
    int socketFd = connectServer(hostname, port);
    LOGV("socketFd = %d", socketFd);

    vector<struct pollfd> fds;
    fds.reserve(1024);

    ASSERT(socketFd >= 0, exit);
    ASSERT( protocol_init(socketFd, commandReadFd, responseWriteFd) == 0, exit);

    for(;;) {
        fds.clear();
        {
            size_t i = fds.size();
            fds.resize(i+1);
            fds[i].fd = socketFd;
        }
        {
            size_t i = fds.size();
            fds.resize(i+1);
            fds[i].fd = commandReadFd;
        }
        if (get_tun_fd() >= 0) {
            size_t i = fds.size();
            fds.resize(i+1);
            fds[i].fd = get_tun_fd();
        }
        for(int i = 0; i < (int)fds.size(); i ++) {
            fds[i].events = POLLRDNORM;
            fds[i].revents = 0;
        }

        int ret = poll(fds.data(), fds.size(), 1000);
        ERROR_CHECK(ret, exit);
        if (handle_heartbeat() != 0) {
            LOGI("exit because of heartbeat timeout");
            goto exit;
        }

        if (ret == 0) {
            continue;
        }

        for(int i = 0; i < (int)fds.size(); i ++) {
            if (!(fds[i].revents & (POLLRDNORM | POLLERR))) continue;
            if (fds[i].fd == commandReadFd) {
                int command = handle_command();
                if (command == IPC_COMMAND_EXIT) {
                    LOGI("exit because of exit command");
                    goto exit;
                }
                if (command < 0) {
                    LOGE("exit because of handle_command command = %d", command);
                    goto exit;
                }
            }
            if (fds[i].fd == socketFd) {
                ASSERT( handle_socket() == 0, exit);
            }
            if (fds[i].fd == get_tun_fd()) {
                ASSERT( handle_tunel() == 0, exit);
            }
        }
    }
    exit:
    LOGV("exit");

    if (socketFd >= 0) close(socketFd);
    if (commandReadFd >= 0) close(commandReadFd);
    if (responseWriteFd >= 0) close(responseWriteFd);
    if (get_tun_fd() >= 0) close(get_tun_fd());

    return 0;
}
