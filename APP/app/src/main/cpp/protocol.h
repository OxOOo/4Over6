//
// Created by chenyu on 2018/5/15.
//

#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#define HEARTBEAT_TIMEOUT_SECS 60
#define HEARTBEAT_INTERVAL_SECS 20

#define IPC_COMMAND_EXIT 1 // 退出命令
#define IPC_COMMAND_FETCH_CONFIG 2 // 获取IP配置信息
#define IPC_COMMAND_SET_TUN 3 // 设置tun
#define IPC_COMMAND_FETCH_STATE 4

// 协议初始化
int protocol_init(int _socketFd, int _commandReadFd, int _responseWriteFd);

int get_tun_fd();

// 处理tun
int handle_tunel();

// 处理socket请求
int handle_socket();

// 处理前端命令
int handle_command();

// 处理心跳
int handle_heartbeat();

#endif //APP_PROTOCOL_H
