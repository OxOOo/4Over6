#include <jni.h>
#include <string>
#include "vpn_main.h"

extern "C" JNIEXPORT jstring

JNICALL
Java_com_a4over6_app_app_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C--";
    return env->NewStringUTF(hello.c_str());
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_a4over6_app_app_VPNThread_vpn_1entry(JNIEnv *env, jobject instance, jstring hostName_,
                                              jint port, jint commandReadFd, jint responseWriteFd) {
    const char *hostName = env->GetStringUTFChars(hostName_, 0);

    int ret = vpn_main(hostName, port, commandReadFd, responseWriteFd);

    env->ReleaseStringUTFChars(hostName_, hostName);

    return ret;
}