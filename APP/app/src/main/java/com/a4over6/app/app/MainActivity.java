package com.a4over6.app.app;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.VpnService;
import android.support.v4.content.LocalBroadcastManager;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.text.format.Formatter;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.util.Enumeration;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.io.DataInputStream;
import java.io.DataOutputStream;

public class MainActivity extends AppCompatActivity implements ConfigDialogFragment.Callback {
    static {
        System.loadLibrary("native-lib");
    }

    private String my_ipv6_addr = "";
    private String my_ipv4_addr = "";

    private String server_ipv6_addr = "";
    private int server_port = 5678;

    private int running_state = 0;
    private String virtual_ipv4_addr = null;
    private long in_bytes = 0;
    private long out_bytes = 0;
    private long in_packets = 0;
    private long out_packets = 0;
    private long in_speed_bytes = 0;
    private long out_speed_bytes = 0;
    private long running_time = 0;

    private BroadcastReceiver vpn_state_receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent.getAction().equals(VPNService.BROADCAST_STATE)) {
                MainActivity.this.running_state = intent.getIntExtra("running_state", 0);
                MainActivity.this.virtual_ipv4_addr = intent.getStringExtra("virtual_ipv4_addr");
                MainActivity.this.in_bytes = intent.getLongExtra("in_bytes", 0);
                MainActivity.this.out_bytes = intent.getLongExtra("out_bytes", 0);
                MainActivity.this.in_packets = intent.getLongExtra("in_packets", 0);
                MainActivity.this.out_packets = intent.getLongExtra("out_packets", 0);
                MainActivity.this.in_speed_bytes = intent.getLongExtra("in_speed_bytes", 0);
                MainActivity.this.out_speed_bytes = intent.getLongExtra("out_speed_bytes", 0);
                MainActivity.this.running_time = intent.getLongExtra("running_time", 0);

                findViewById(R.id.connect_button).setEnabled(true);
                findViewById(R.id.restart_button).setEnabled(true);
            }
            updateUI();
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        findViewById(R.id.connect_button).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                findViewById(R.id.connect_button).setEnabled(false);
                findViewById(R.id.restart_button).setEnabled(false);
                findViewById(R.id.config_button).setEnabled(false);

                if (running_state == 0) {
                    prepareVPNService();
                } else {
                    stopVPNService();
                }
            }
        });

        findViewById(R.id.restart_button).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                findViewById(R.id.connect_button).setEnabled(false);
                findViewById(R.id.restart_button).setEnabled(false);
                findViewById(R.id.config_button).setEnabled(false);

                stopVPNService();
                prepareVPNService();
            }
        });

        findViewById(R.id.config_button).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                showViewDialogFragment(v);
            }
        });

        IntentFilter intentFilter = new IntentFilter();
        intentFilter.addAction(VPNService.BROADCAST_STATE);
        LocalBroadcastManager.getInstance(this).registerReceiver(vpn_state_receiver, intentFilter);
    }

    @Override
    protected void onPostResume() {
        super.onPostResume();
        loadConfig();
        updateLocalIpAddress();
        updateUI();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode == RESULT_OK) {
            startVPNService();
        }
    }

    private void prepareVPNService() {
        Intent intent = VpnService.prepare(MainActivity.this);
        if (intent != null) {
            startActivityForResult(intent, 0);
        } else {
            onActivityResult(0, RESULT_OK, null);
        }
    }

    private void startVPNService() {
        Intent intent = new Intent(MainActivity.this, VPNService.class);
        intent.putExtra("command", "start");
        intent.putExtra("hostname", server_ipv6_addr);
        intent.putExtra("port", server_port);
        startService(intent);
    }

    private void stopVPNService() {
        Intent intent = new Intent(MainActivity.this, VPNService.class);
        intent.putExtra("command", "stop");
        startService(intent);
    }

    private void loadConfig() {
        try {
            DataInputStream file = new DataInputStream(this.openFileInput("server_ipv6_addr"));
            server_ipv6_addr = file.readUTF();
            file.close();
        } catch (java.io.IOException e) {
            e.printStackTrace();
        }
        try {
            DataInputStream file = new DataInputStream(this.openFileInput("server_port"));
            server_port = Integer.valueOf(file.readUTF());
            file.close();
        } catch (java.io.IOException e) {
            e.printStackTrace();
        }
    }

    private void saveConfig() {
        try {
            DataOutputStream file = new DataOutputStream(this.openFileOutput("server_ipv6_addr", Context.MODE_PRIVATE));
            file.writeUTF(server_ipv6_addr);
            file.close();
        } catch (java.io.IOException e) {
            e.printStackTrace();
        }
        try {
            DataOutputStream file = new DataOutputStream(this.openFileOutput("server_port", Context.MODE_PRIVATE));
            file.writeUTF(String.valueOf(server_port));
            file.close();
        } catch (java.io.IOException e) {
            e.printStackTrace();
        }
    }

    private void updateUI() {
        Button connect_btn = (Button)findViewById(R.id.connect_button);
        connect_btn.setText(running_state == 0 ? "Connect" : "Disconnect");

        TextView ip_info = (TextView) findViewById(R.id.ip_info_text);
        ip_info.setText(
                " local IPv4 : " + my_ipv4_addr + "\n" +
                " local IPv6 : " + my_ipv6_addr + "\n" +
                " remote IPv6 : " + server_ipv6_addr + "\n" +
                " remote port : " + server_port + "\n"+
                " virtual IPv4 : " + virtual_ipv4_addr + "\n");

        TextView log_info = (TextView)findViewById(R.id.log_info_text);
        log_info.setText(
                " " + (running_state == 0 ? "Disconnected" : "Connected") + "\n" +
                " Running Time : " + int2time(running_time) + "\n" +
                " -----------------------\n" +
                " Up : " + Formatter.formatShortFileSize(MainActivity.this, out_bytes) + "\n" +
                " Up Speed : " + Formatter.formatShortFileSize(MainActivity.this, out_speed_bytes) + "/s\n" +
                " Up Packets : " + out_packets + "\n" +
                " -----------------------\n" +
                " Down : " + Formatter.formatShortFileSize(MainActivity.this, in_bytes) + "\n" +
                " Down Speed : " + Formatter.formatShortFileSize(MainActivity.this, in_speed_bytes) + "/s\n" +
                " Down Packets : " + in_packets + "\n"
        );

        if (running_state == 0) {
            findViewById(R.id.config_button).setEnabled(true);
            findViewById(R.id.restart_button).setEnabled(false);
        } else {
            findViewById(R.id.config_button).setEnabled(false);
            findViewById(R.id.restart_button).setEnabled(true);
        }
    }

    private String int2time(long duration) {
        long second = duration % 60;
        long minute = (duration / 60) % 60;
        long hour = duration / 3600;
        return String.format("%d:%02d:%02d", hour, minute, second);
    }

    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();

    public void updateLocalIpAddress() {
        try {
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
                        }
                        else if(ipv6_matcher.find())
                        {
                            my_ipv6_addr = inetAddress.getHostAddress().toString().substring(0, ipv6_matcher.start());
                        }
                    }
                }
            }
        } catch (SocketException e) {
            e.printStackTrace();
        }
    }

    // Belows are for dialog
    public void showViewDialogFragment(View view) {
        ConfigDialogFragment dialog = new ConfigDialogFragment();
        Bundle args = new Bundle();
        args.putString("addr", server_ipv6_addr);
        args.putInt("port", server_port);
        dialog.setArguments(args);

        dialog.show(getFragmentManager());
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
            server_port = Integer.valueOf(port);
            updateUI();
            saveConfig();
        }
        else Toast.makeText(MainActivity.this, "Invalid Input!", Toast.LENGTH_SHORT).show();
    }
}
