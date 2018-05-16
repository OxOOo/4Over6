package com.a4over6.app.app;

import android.util.Log;

public class VPNThread implements Runnable {
    private String hostname;
    private int port;
    private int commandReadFd;
    private int responseWriteFd;

    VPNThread(String hostname, int port, int commandReadFd, int responseWriteFd) {
        this.hostname = hostname;
        this.port = port;
        this.commandReadFd = commandReadFd;
        this.responseWriteFd = responseWriteFd;
    }

    @Override
    public void run() {
        Log.d("VPNThread", "run");
        int rst = vpn_entry(hostname, port, commandReadFd, responseWriteFd);
        Log.d("VPNThread", "rst="+rst);
    }

    public native int vpn_entry(String hostName, int port, int commandReadFd, int responseWriteFd);

    static {
        System.loadLibrary("native-lib");
    }
}
