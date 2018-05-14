package com.a4over6.app.app;

import android.content.Intent;
import android.net.VpnService;
import android.util.Log;

public class VPNService extends VpnService {
    @Override
    public void onCreate() {
        super.onCreate();
        Log.d("debug", "OnCreate");
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d("debug", "onStartCommand");

        new Thread(new VPNThread()).start();

        return START_REDELIVER_INTENT;
    }
}
