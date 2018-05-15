package com.a4over6.app.app;

import android.app.Service;
import android.content.Intent;
import android.net.VpnService;
import android.os.ParcelFileDescriptor;

import java.util.TimerTask;

public class MyVpnService extends VpnService {

    @Override
    public void onCreate(){

    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId){
        TimerTask timer = new TimerTask() {
            @Override
            public void run() {
                Builder builder = new Builder();
//                builder.setMtu(1000);
//                builder.addAddress();
//                builder.addRoute("0.0.0.0",0);
//                builder.addDnsServer();
//                builder.addSearchDomain();
//                builder.setSession();
                ParcelFileDescriptor inter = builder.establish();
            }
        };

        return Service.START_REDELIVER_INTENT;
    }

    @Override
    public void onDestroy(){

    }

}
