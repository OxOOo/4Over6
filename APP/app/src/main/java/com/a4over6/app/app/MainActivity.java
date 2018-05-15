package com.a4over6.app.app;

import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.VpnService;
import android.os.Handler;
import android.os.Message;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.util.Enumeration;
import java.util.Timer;
import java.util.TimerTask;
import java.util.regex.Matcher;
import java.util.regex.Pattern;


public class MainActivity extends AppCompatActivity implements ViewDialogFragment.Callback {

    // Used to load the 'native-lib' library on application startup.
    int connecting_state = 0;
    String my_ipv6_addr = "";
    String my_ipv4_addr = "";
    String virtual_ipv4_addr = "";
    String server_ipv6_addr = "";
    String server_port = "";
    int duration = 0;

    protected Timer timer = null;

    static {
        System.loadLibrary("native-lib");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {

        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Example of a call to a native method
        TextView tv = (TextView) findViewById(R.id.sample_text);
        //tv.setText(stringFromJNI());
        tv.setText(String.valueOf(getLocalIpAddress()));
        findViewById(R.id.connect_button).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if(connecting_state == 0){
                    ((Button)v).setText("Disconnect");
                    // Start the VPN service here
                }
                else {
                    ((Button) v).setText("Connect");
                    //Shut down the VPN service here
                }
                connecting_state = 1 - connecting_state;
            }
        });

        findViewById(R.id.restart_button).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                duration = 0;
                connecting_state = 0;
                my_ipv4_addr = "";
                my_ipv6_addr = "";
                server_ipv6_addr = "";
                server_port = "";
                ((TextView)findViewById(R.id.log_info)).setText("");
                //Restart the VPN service here
            }
        });

        findViewById(R.id.config_button).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                showViewDialogFragment(v);
            }
        });

        /* VPN
        Intent intent = VpnService.prepare(this);
        if(intent != null){
            startActivityForResult(intent, 0);
        }else{
            onActivityResult(0, RESULT_OK, null);
        }*/

        final Handler handler = new Handler(){
            @Override
            public void handleMessage(Message msg) {
                switch (msg.what){
                    case 1:
                        update_connection_into();
                        break;
                }
                super.handleMessage(msg);
            }
        };

        TimerTask task = new TimerTask(){
            @Override
            public void run() {
                Message message = new Message();
                message.what = 1;
                handler.sendMessage(message);
            }
        };

        timer = new Timer();
        timer.scheduleAtFixedRate(task, 1000, 1000);


    }

    protected void onActivityResult(int request, int result, Intent data) {
        if (result == RESULT_OK) {
            Intent intent = new Intent(this, MyVpnService.class);
            startService(intent);
        }
    }

    private void update_connection_into() {
        TextView tv = (TextView) findViewById(R.id.sample_text);
        //tv.setText(stringFromJNI());
        if(connecting_state == 1) duration += 1;
        getLocalIpAddress();
        tv.setText(" local IPv4 : " + my_ipv4_addr + "\n local IPv6 : " + my_ipv6_addr +
                "\n remote IPv6 : " + server_ipv6_addr + " \n port : " + server_port + "\n virtual IPv4 : "
                + virtual_ipv4_addr + "\n duration : " + int2time(duration));
    }

    private String int2time(int duration) {
        int second = duration % 60;
        int minute = (duration / 60) % 60;
        int hour = duration / 3600;
        return String.format("%d:%02d:%02d", hour, minute, second);
    }


    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();

    public static String isNetworkAvailable(Context context) {
        ConnectivityManager cm = (ConnectivityManager) context
                .getSystemService(Context.CONNECTIVITY_SERVICE);
        String str = "";
        if (cm == null) {
        } else {
            NetworkInfo[] info = cm.getAllNetworkInfo();
            if (info != null) {
                for (int i = 0; i < info.length; i++) {
                    str += info[i].toString() + "\n";
//                    if (info[i].getState() == NetworkInfo.State.CONNECTED) {
//                        return true;
//                    }
                }
            }
        }
        return str;
    }

    public String getLocalIpAddress() {
        try {
            String str = "";
            my_ipv4_addr = "";
            my_ipv6_addr = "";
            for (Enumeration<NetworkInterface> en = NetworkInterface.getNetworkInterfaces(); en.hasMoreElements();) {
                NetworkInterface intf = en.nextElement();
                for (Enumeration<InetAddress> enumIpAddr = intf.getInetAddresses(); enumIpAddr.hasMoreElements();) {
                    InetAddress inetAddress = enumIpAddr.nextElement();
                    if (!inetAddress.isLoopbackAddress()) {
                        Matcher ipv4_matcher = Pattern.compile("[.]").matcher(inetAddress.getHostAddress().toString());
                        Matcher ipv6_matcher = Pattern.compile("%").matcher(inetAddress.getHostAddress().toString());
                        if(ipv4_matcher.find())
                        {
                            my_ipv4_addr = inetAddress.getHostAddress().toString();
                            str +=  "4";
                        }
                        else if(ipv6_matcher.find())
                        {
                            my_ipv6_addr = inetAddress.getHostAddress().toString().substring(0, ipv6_matcher.start());
                            str +=  "6";
                        }
                        //str +=  inetAddress.getHostAddress().toString() + "\n";
                    }
                }
            }
            return str;
        } catch (SocketException ex) {

        }
        return null;
    }


    // Belows are for dialog
    public void showViewDialogFragment(View view) {
        ViewDialogFragment viewDialogFragment = new ViewDialogFragment();
        viewDialogFragment.show(getFragmentManager());
    }

    @Override
    public void onClick(String ipv6_addr, String port) {
        Matcher addr_matcher = Pattern.compile("^((([0-9A-Fa-f]{1,4}:){7}[0-9A-Fa-f]{1,4})|" +
                "(([0-9A-Fa-f]{1,4}:){1,7}:)|(([0-9A-Fa-f]{1,4}:){6}:[0-9A-Fa-f]{1,4})|" +
                "(([0-9A-Fa-f]{1,4}:){5}(:[0-9A-Fa-f]{1,4}){1,2})|(([0-9A-Fa-f]{1,4}:){4}(" +
                ":[0-9A-Fa-f]{1,4}){1,3})|(([0-9A-Fa-f]{1,4}:){3}(:[0-9A-Fa-f]{1,4}){1,4})|" +
                "(([0-9A-Fa-f]{1,4}:){2}(:[0-9A-Fa-f]{1,4}){1,5})|([0-9A-Fa-f]{1,4}:(:[0-9A-Fa-f]" +
                "{1,4}){1,6})|(:(:[0-9A-Fa-f]{1,4}){1,7})|(([0-9A-Fa-f]{1,4}:){6}(\\d|[1-9]\\d|1\\d{2}" +
                "|2[0-4]\\d|25[0-5])(\\.(\\d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])){3})|(([0-9A-Fa-f]{1,4}:)" +
                "{5}:(\\d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])(\\.(\\d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])){3})" +
                "|(([0-9A-Fa-f]{1,4}:){4}(:[0-9A-Fa-f]{1,4}){0,1}:(\\d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])" +
                "(\\.(\\d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])){3})|(([0-9A-Fa-f]{1,4}:){3}(:[0-9A-Fa-f]{1,4}" +
                "){0,2}:(\\d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])(\\.(\\d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5]))" +
                "{3})|(([0-9A-Fa-f]{1,4}:){2}(:[0-9A-Fa-f]{1,4}){0,3}:(\\d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])(" +
                "\\.(\\d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])){3})|([0-9A-Fa-f]{1,4}:(:[0-9A-Fa-f]{1,4}){0,4}:(\\" +
                "d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])(\\.(\\d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])){3})|(:(" +
                ":[0-9A-Fa-f]{1,4}){0,5}:(\\d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])(\\.(\\d|[1-9]\\d|1\\d{2}|2[0-" +
                "4]\\d|25[0-5])){3}))$").matcher(ipv6_addr);
        Matcher port_matcher = Pattern.compile("[0-9]{4}").matcher(port);
        if(addr_matcher.matches() && port_matcher.matches())
        {
            server_ipv6_addr = ipv6_addr;
            server_port = port;
        }
        else Toast.makeText(MainActivity.this, "Invalid Input!", Toast.LENGTH_SHORT).show();
    }
}
