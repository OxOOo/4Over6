//
// Created by chenyu on 2018/5/15.
//

#ifndef APP_VPN_MAIN_H
#define APP_VPN_MAIN_H

#include <string>

int vpn_main(const std::string hostname, int port, int commandReadFd, int responseWriteFd);

#endif //APP_VPN_MAIN_H
