package com.zuoqirun.amapesp32forwarder;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;

public final class ForwarderService extends Service implements AMapBroadcastReceiver.Listener {
    static final String ACTION_START = "com.zuoqirun.amapesp32forwarder.START";
    static final String ACTION_STOP = "com.zuoqirun.amapesp32forwarder.STOP";
    static final String ACTION_SEND_TEST = "com.zuoqirun.amapesp32forwarder.SEND_TEST";

    private static final String CHANNEL_ID = "amap_esp32_forwarder";
    private static final int NOTIFICATION_ID = 4210;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable heartbeat = new Runnable() {
        @Override
        public void run() {
            if (AppSettings.isEnabled(ForwarderService.this)) {
                forwarder.send(aggregator.snapshot(), true);
                handler.postDelayed(this, 1000L);
            }
        }
    };

    private AMapBroadcastReceiver receiver;
    private AMapStateAggregator aggregator;
    private Esp32UdpForwarder forwarder;

    @Override
    public void onCreate() {
        super.onCreate();
        aggregator = new AMapStateAggregator();
        forwarder = new Esp32UdpForwarder(this);
        startForeground(NOTIFICATION_ID, buildNotification());
        registerAmapReceiver();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? ACTION_START : intent.getAction();
        if (ACTION_STOP.equals(action)) {
            stopSelf();
            return START_NOT_STICKY;
        }
        if (ACTION_SEND_TEST.equals(action)) {
            AppSettings.setEnabled(this, true);
            forwarder.send(Esp32NavState.testFrame(), true);
        }
        if (!AppSettings.isEnabled(this)) {
            stopSelf();
            return START_NOT_STICKY;
        }
        handler.removeCallbacks(heartbeat);
        handler.post(heartbeat);
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        try {
            if (receiver != null) {
                unregisterReceiver(receiver);
            }
        } catch (Throwable ignored) {
        }
        if (forwarder != null) {
            forwarder.shutdown();
        }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onAmapBroadcast(Intent intent) {
        AppSettings.noteBroadcast(this);
        Esp32NavState snapshot = aggregator.handleIntent(intent);
        forwarder.send(snapshot, false);
    }

    private void registerAmapReceiver() {
        receiver = new AMapBroadcastReceiver(this);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(receiver, AMapBroadcastReceiver.createFilter(), Context.RECEIVER_EXPORTED);
        } else {
            registerReceiver(receiver, AMapBroadcastReceiver.createFilter());
        }
    }

    private Notification buildNotification() {
        NotificationManager manager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && manager != null) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "AMap ESP32 Forwarder",
                    NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("Forward AMap Auto navigation state to ESP32 over UDP.");
            manager.createNotificationChannel(channel);
        }
        Intent open = new Intent(this, MainActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                this,
                0,
                open,
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0);
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        return builder
                .setSmallIcon(android.R.drawable.stat_sys_upload)
                .setContentTitle("AMap ESP32 Forwarder")
                .setContentText("正在监听高德广播并转发导航快照")
                .setContentIntent(pendingIntent)
                .setOngoing(true)
                .build();
    }
}
