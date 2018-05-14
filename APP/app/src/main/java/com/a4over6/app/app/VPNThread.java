package com.a4over6.app.app;

public class VPNThread implements Runnable {
    @Override
    public void run() {
        vpn_entry("2402:f000:1:4417::900", 5678, 0, 0);
    }

    public native int vpn_entry(String hostName, int port, int commandReadFd, int responseWriteFd);

    static {
        System.loadLibrary("native-lib");
    }
}
