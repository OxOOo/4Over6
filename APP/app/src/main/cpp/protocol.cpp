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
#define PROTOCOL_HARTBEAT 104
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

static time_t last_hartbeat_from_server;
static time_t last_hartbeat_to_server;

void protocol_init(int _socketFd, int _commandReadFd, int _responseWriteFd)
{
    // 内存布局必须符合预期
    assert( &(( (Message*)(void*)(0) )->type) - (uint8_t*)(0) == 4);
    assert(sizeof(Message) == 5);

    socketFd = _socketFd;
    commandReadFd = _commandReadFd;
    responseWriteFd = _responseWriteFd;
    tunFd = -1;

    last_hartbeat_from_server = time(NULL);
    last_hartbeat_to_server = time(NULL);

    socket_nreads = 0;
}

int get_tun_fd()
{
    return tunFd;
}

void handle_tunel()
{
    static uint8_t buffer[BUFFER_SIZE];
    ssize_t size = read(tunFd, buffer, BUFFER_SIZE);
    ERROR_CHECK(size);
    if (size == 0) return;

    Message msg;
    msg.length = sizeof(Message)+size;
    msg.type = PROTOCOL_PACKET_SEND;
    ssize_t size1 = write(socketFd, &msg, sizeof(msg));
    assert(size1 == sizeof(msg));
    ssize_t size2 = write(socketFd, buffer, size);
    assert(size2 == size);

    // LOGD("handle_tunel size = %d, size1 = %d, size2 = %d", size, size1, size2);
}

void handle_socket()
{
    LOGD("handle_socket");
    LOGD("before socket_nreads = %d", socket_nreads);

    ssize_t size = recv(socketFd, socket_buffer+socket_nreads, BUFFER_SIZE-socket_nreads, MSG_DONTWAIT);
    ERROR_CHECK(size);
    socket_nreads += size;
    assert(socket_nreads >= 0);

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

            assert(write(responseWriteFd, ip, 4) == 4);
            assert(write(responseWriteFd, mask, 4) == 4);
            assert(write(responseWriteFd, dns1, 4) == 4);
            assert(write(responseWriteFd, dns2, 4) == 4);
            assert(write(responseWriteFd, dns3, 4) == 4);
            assert(write(responseWriteFd, &socketFd, 4) == 4);
        } else if (msg.type == PROTOCOL_HARTBEAT) {
            LOGV("PROTOCOL_HARTBEAT");
            last_hartbeat_from_server = time(NULL);
        } else if (msg.type == PROTOCOL_PACKET_RECV) {
            LOGV("PROTOCOL_PACKET_RECV");
            write(tunFd, socket_buffer+sizeof(Message), msg.length);
        } else {
            LOGW("unknow type = %d", msg.type);
        }
        memmove(socket_buffer, socket_buffer+msg.length, socket_nreads-msg.length);
        LOGD("here1 socket_nreads=%d", socket_nreads);
        socket_nreads -= msg.length;
        LOGD("here2 socket_nreads=%d", socket_nreads);
    }
}

int handle_command()
{
    uint8_t command;
    ssize_t ret = read(commandReadFd, &command, 1);
    ERROR_CHECK(ret);
    if (ret != 1) return ret;
    if (command == IPC_COMMAND_EXIT) {
        LOGV("IPC_COMMAND_EXIT");

        return IPC_COMMAND_EXIT;
    } else if (command == IPC_COMMAND_FETCH_CONFIG) {
        LOGV("IPC_COMMAND_FETCH_CONFIG");

        Message msg;
        msg.length = sizeof(Message);
        msg.type = PROTOCOL_IP_REQUEST;
        assert(write(socketFd, &msg, sizeof(msg)) == sizeof(msg));
    } else if (command == IPC_COMMAND_SET_TUN) {
        LOGV("IPC_COMMAND_SET_TUN");

        uint8_t *data = (uint8_t*)&tunFd;
        int nreads = 0;
        while(nreads < 4) {
            ssize_t ret = read(commandReadFd, data+nreads, 4-nreads);
            ERROR_CHECK(ret);
            nreads += ret;
        }
        LOGV("tunFd = %d", tunFd);
        assert(tunFd >= 0);
    }
    return command;
}

int handle_hartbeat()
{
    time_t now = time(NULL);
    if (now - last_hartbeat_from_server > HARTBEAT_TIMEOUT_SECS) return -1;
    if (now - last_hartbeat_to_server > HARTBEAT_INTERVAL_SECS) {
        Message msg;
        msg.length = sizeof(Message);
        msg.type = PROTOCOL_HARTBEAT;
        write(socketFd, &msg, sizeof(msg));
        last_hartbeat_to_server = now;
    }

    return 0;
}
