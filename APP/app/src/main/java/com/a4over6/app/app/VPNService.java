package com.a4over6.app.app;

import android.content.Intent;
import android.net.VpnService;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.net.InetAddress;
import java.util.Arrays;

public class VPNService extends VpnService {
    private Thread vpnThread;
    private ParcelFileDescriptor vpnInterface;

    private ParcelFileDescriptor commandWriteFd;
    private ParcelFileDescriptor commandReadFd;
    private ParcelFileDescriptor responseWriteFd;
    private ParcelFileDescriptor responseReadFd;

    private DataInputStream responseReadStream;
    private DataOutputStream commandWriteStream;

    @Override
    public void onCreate() {
        super.onCreate();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d("debug", "onStartCommand");

        startVPN(intent.getStringExtra("hostname"));

        return START_REDELIVER_INTENT;
    }

    private byte[] readResponse(int length) throws IOException {
        byte[] data = new byte[length];
        int offset = 0;
        while(offset < length) {
            int read = responseReadStream.read(data, offset, length-offset);
            offset += read;
        }
        return data;
    }

    private int readInt(DataInputStream s) throws IOException {
        int a = s.read();
        int b = s.read();
        int c = s.read();
        int d = s.read();
        return ((d*256+c)*256+b)*256+a;
    }
    private void writeInt(DataOutputStream s, int data) throws IOException {
        int a = data%256; data /= 256;
        int b = data%256; data /= 256;
        int c = data%256; data /= 256;
        int d = data%256; data /= 256;
        s.write(a);s.write(b);s.write(c);s.write(d);
    }

    private int startVPN(String hostname) {
        stopVPN();

        try {
            ParcelFileDescriptor[] pipeFds = ParcelFileDescriptor.createPipe();
            commandWriteFd = pipeFds[1];
            commandReadFd = pipeFds[0];
            pipeFds = ParcelFileDescriptor.createPipe();
            responseWriteFd = pipeFds[1];
            responseReadFd = pipeFds[0];
        } catch (IOException e) {
            e.printStackTrace();
            return -1;
        }

        //vpnThread = new Thread(new VPNThread("2402:f000:1:440b:1618:77ff:fe2d:fbc4", 5678, commandReadFd.getFd(), responseWriteFd.getFd()));
        vpnThread = new Thread(new VPNThread(hostname, 5678, commandReadFd.getFd(), responseWriteFd.getFd()));
        vpnThread.start();

        commandWriteStream = new DataOutputStream(new FileOutputStream(commandWriteFd.getFileDescriptor()));
        responseReadStream = new DataInputStream(new FileInputStream(responseReadFd.getFileDescriptor()));

        // 初始化配置
        try {
            commandWriteStream.writeByte(IPC.IPC_COMMAND_FETCH_CONFIG);
            byte[] data = readResponse(20);
            InetAddress address = InetAddress.getByAddress(Arrays.copyOfRange(data, 0, 4));
            InetAddress mask = InetAddress.getByAddress(Arrays.copyOfRange(data, 4, 8));
            InetAddress dns1 = InetAddress.getByAddress(Arrays.copyOfRange(data, 8, 12));
            InetAddress dns2 = InetAddress.getByAddress(Arrays.copyOfRange(data, 12, 16));
            InetAddress dns3 = InetAddress.getByAddress(Arrays.copyOfRange(data, 16, 20));
            int socketFd = readInt(responseReadStream);

            vpnInterface = new Builder()
                    .addAddress(address, 24)
                    .addDnsServer(dns1)
                    .addDnsServer(dns2)
                    .addDnsServer(dns3)
                    .addRoute("0.0.0.0", 0)
                    .setSession("4over6")
                    .establish();
            commandWriteStream.writeByte(IPC.IPC_COMMAND_SET_TUN);
            writeInt(commandWriteStream, vpnInterface.getFd());
            protect(socketFd);
        } catch (IOException e) {
            e.printStackTrace();
            return -1;
        }

        return 0;
    }

    private void stopVPN() {
        if (vpnThread != null && vpnThread.isAlive()) {
            try {
                commandWriteStream.writeByte(IPC.IPC_COMMAND_EXIT);
                vpnInterface.close();
                commandWriteStream.close();
                responseReadStream.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
            try {
                vpnThread.join(2000);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        vpnThread = null;
    }
}
