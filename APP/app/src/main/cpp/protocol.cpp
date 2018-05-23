//
// Created by chenyu on 2018/5/15.
//

#include "protocol.h"
#include "common.h"
#include <android/log.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>

using namespace std;

#define PROTOCOL_IP_REQUEST 100
#define PROTOCOL_IP_REPLY 101
#define PROTOCOL_PACKET_SEND 102
#define PROTOCOL_PACKET_RECV 103
#define PROTOCOL_HEARTBEAT 104
typedef struct
{
    int length;
    uint8_t type;
} __attribute__((packed)) Message;

static const int BUFFER_SIZE = 1024*1024*4;
static uint8_t socket_buffer[BUFFER_SIZE];
static ssize_t socket_nreads;

static int socketFd;
static int commandReadFd;
static int responseWriteFd;
static int tunFd;

static time_t last_heartbeat_from_server;
static time_t last_heartbeat_to_server;

uint64_t in_bytes;
uint64_t out_bytes;
uint64_t in_packets;
uint64_t out_packets;

int protocol_init(int _socketFd, int _commandReadFd, int _responseWriteFd)
{
    // 内存布局必须符合预期
    ASSERT( &(( (Message*)(void*)(0) )->type) - (uint8_t*)(0) == 4, fail);
    ASSERT(sizeof(Message) == 5, fail);

    socketFd = _socketFd;
    commandReadFd = _commandReadFd;
    responseWriteFd = _responseWriteFd;
    tunFd = -1;

    last_heartbeat_from_server = time(NULL);
    last_heartbeat_to_server = time(NULL);

    socket_nreads = 0;

    in_bytes = 0;
    out_bytes = 0;
    in_packets = 0;
    out_packets = 0;

    return 0;
    fail:
    return -1;
}

int get_tun_fd()
{
    return tunFd;
}

int handle_tunel()
{
    static uint8_t buffer[BUFFER_SIZE];
    ssize_t size = read(tunFd, buffer, BUFFER_SIZE);
    ERROR_CHECK(size, fail);
    if (size == 0) return 0;

    Message msg;
    msg.length = sizeof(Message)+size;
    msg.type = PROTOCOL_PACKET_SEND;
    ASSERT(write(socketFd, &msg, sizeof(msg)) == sizeof(msg), fail);
    ASSERT(write(socketFd, buffer, size) == size, fail);
    out_packets ++;
    out_bytes += size;

    return 0;
    fail:
    return -1;
}

int handle_socket()
{
    ssize_t size = recv(socketFd, socket_buffer+socket_nreads, BUFFER_SIZE-socket_nreads, MSG_DONTWAIT);
    ERROR_CHECK(size, fail);
    socket_nreads += size;
    ASSERT(socket_nreads >= 0, fail);

    LOGD("handle_socket socket_nreads=%d", socket_nreads);

    while(true)
    {
        if (socket_nreads < sizeof(Message)) break;
        Message msg = *(Message*)socket_buffer;
        if (socket_nreads < msg.length) break;

        LOGV("type = %d, length = %d", msg.type, msg.length);

        if (msg.type == PROTOCOL_IP_REPLY) {
            LOGV("PROTOCOL_IP_REPLY");

            vector<char> data;
            data.resize(msg.length-sizeof(Message));
            memmove(data.data(), socket_buffer+sizeof(Message), msg.length-sizeof(Message));
            LOGV("PROTOCOL_IP_REPLY: %s", data.data());

            const char *ptr = data.data();
            uint8_t ip[4], mask[4], dns1[4], dns2[4], dns3[4];
            ptr = READ_IP(ptr, ip);
            ptr = READ_IP(ptr, mask);
            ptr = READ_IP(ptr, dns1);
            ptr = READ_IP(ptr, dns2);
            ptr = READ_IP(ptr, dns3);

            ASSERT(write(responseWriteFd, ip, 4) == 4, fail);
            ASSERT(write(responseWriteFd, mask, 4) == 4, fail);
            ASSERT(write(responseWriteFd, dns1, 4) == 4, fail);
            ASSERT(write(responseWriteFd, dns2, 4) == 4, fail);
            ASSERT(write(responseWriteFd, dns3, 4) == 4, fail);
            ASSERT(write(responseWriteFd, &socketFd, 4) == 4, fail);
        } else if (msg.type == PROTOCOL_HEARTBEAT) {
            LOGV("PROTOCOL_HEARTBEAT");
            last_heartbeat_from_server = time(NULL);
        } else if (msg.type == PROTOCOL_PACKET_RECV) {
            LOGV("PROTOCOL_PACKET_RECV");
            if (tunFd >= 0) {
                ASSERT(write(tunFd, socket_buffer+sizeof(Message), msg.length) == msg.length, fail);
                in_bytes += msg.length;
                in_packets ++;
            } else {
                LOGW("recved packet before set tunFd");
            }
        } else {
            LOGW("unknow type = %d", msg.type);
            ASSERT(false, fail);
        }
        memmove(socket_buffer, socket_buffer+msg.length, socket_nreads-msg.length);
        socket_nreads -= msg.length;
    }

    return 0;
    fail:
    return -1;
}

int handle_command()
{
    uint8_t command;
    ssize_t ret = read(commandReadFd, &command, 1);
    ERROR_CHECK(ret, fail);
    ASSERT(ret == 1, fail);

    if (command == IPC_COMMAND_EXIT) {
        LOGV("IPC_COMMAND_EXIT");
        return 1;
    } else if (command == IPC_COMMAND_FETCH_CONFIG) {
        LOGV("IPC_COMMAND_FETCH_CONFIG");

        Message msg;
        msg.length = sizeof(Message);
        msg.type = PROTOCOL_IP_REQUEST;
        ASSERT(write(socketFd, &msg, sizeof(msg)) == sizeof(msg), fail);
    } else if (command == IPC_COMMAND_SET_TUN) {
        LOGV("IPC_COMMAND_SET_TUN");

        uint8_t *data = (uint8_t*)&tunFd;
        int nreads = 0;
        while(nreads < 4) {
            ssize_t ret = read(commandReadFd, data+nreads, 4-nreads);
            ERROR_CHECK(ret, fail);
            nreads += ret;
        }
        LOGV("tunFd = %d", tunFd);
        ASSERT(tunFd >= 0, fail);
    } else if (command == IPC_COMMAND_FETCH_STATE) {
        LOGV("IPC_COMMAND_FETCH_STATE");

        ASSERT(write(responseWriteFd, &in_bytes, sizeof(uint64_t)) == sizeof(uint64_t), fail);
        ASSERT(write(responseWriteFd, &out_bytes, sizeof(uint64_t)) == sizeof(uint64_t), fail);
        ASSERT(write(responseWriteFd, &in_packets, sizeof(uint64_t)) == sizeof(uint64_t), fail);
        ASSERT(write(responseWriteFd, &out_packets, sizeof(uint64_t)) == sizeof(uint64_t), fail);
    }

    return 0;
    fail:
    return -1;
}

int handle_heartbeat()
{
    time_t now = time(NULL);
    if (now - last_heartbeat_from_server > HEARTBEAT_TIMEOUT_SECS) return -1;
    if (now - last_heartbeat_to_server > HEARTBEAT_INTERVAL_SECS) {
        Message msg;
        msg.length = sizeof(Message);
        msg.type = PROTOCOL_HEARTBEAT;
        ASSERT(write(socketFd, &msg, sizeof(msg)) == sizeof(msg), fail);
        last_heartbeat_to_server = now;
    }

    return 0;
    fail:
    return -1;
}
