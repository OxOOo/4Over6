//
// Created by chenyu on 2018/5/15.
//

#ifndef APP_COMMON_H
#define APP_COMMON_H

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <android/log.h>
#include <cassert>

#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "vpn_backend", __VA_ARGS__);
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "vpn_backend", __VA_ARGS__);
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "vpn_backend", __VA_ARGS__);
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "vpn_backend", __VA_ARGS__);
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "vpn_backend", __VA_ARGS__);

#define ERROR_CHECK(code) \
{ \
    int __ret = (code); \
    if (__ret < 0) { \
        LOGE("error at %s:%d error=%s", __FILE__, __LINE__, strerror(errno));\
    } \
    assert(__ret >= 0); \
}

const char* READ_IP(const char* ptr, uint8_t ip[4]);

#endif //APP_COMMON_H
