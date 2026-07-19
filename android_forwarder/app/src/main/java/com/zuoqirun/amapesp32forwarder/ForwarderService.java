package com.zuoqirun.amapesp32forwarder;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;
import android.net.wifi.WifiManager;

public final class ForwarderService extends Service implements AMapBroadcastReceiver.Listener {
    static final String ACTION_START = "com.zuoqirun.amapesp32forwarder.START";
    static final String ACTION_STOP = "com.zuoqirun.amapesp32forwarder.STOP";
    static final String ACTION_SEND_TEST = "com.zuoqirun.amapesp32forwarder.SEND_TEST";

    private static final String CHANNEL_ID = "amap_esp32_forwarder_live";
    private static final int NOTIFICATION_ID = 4210;
    private static final String TAG = "AMapEsp32Forwarder";
    // AMap Auto replies to an ACTION_RECV query with a snapshot addressed to
    // the querying package. Passive broadcasts alone omit cruise-light data
    // on several head-unit builds, and coexistence builds use other packages.

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable heartbeat = new Runnable() {
        @Override
        public void run() {
            if (AppSettings.isEnabled(ForwarderService.this)) {
                forwarder.sendMusicUpdate(withMusic(aggregator.snapshot()));
                handler.postDelayed(this, MusicStateStore.isActive() ? 200L : 1000L);
            }
        }
    };

    private final Runnable amapPoll = new Runnable() {
        @Override
        public void run() {
            if (AppSettings.isEnabled(ForwarderService.this)) {
                requestAmapSnapshots(false);
                handler.postDelayed(this, 6000L);
            }
        }
    };

    private AMapBroadcastReceiver receiver;
    private AMapStateAggregator aggregator;
    private Esp32UdpForwarder forwarder;
    private PowerManager.WakeLock wakeLock;
    private WifiManager.WifiLock wifiLock;

    @Override
    public void onCreate() {
        super.onCreate();
        aggregator = new AMapStateAggregator();
        forwarder = new Esp32UdpForwarder(this);
        MusicStateStore.initialize(this);
        startForeground(NOTIFICATION_ID, buildNotification());
        acquireBackgroundLocks();
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
        handler.removeCallbacks(amapPoll);
        requestAmapSnapshots(true);
        handler.postDelayed(amapPoll, 6000L);
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
        releaseBackgroundLocks();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onAmapBroadcast(Intent intent) {
        AppSettings.noteBroadcast(this);
        String action = intent == null ? "" : intent.getAction();
        boolean trafficInput = intent != null && (action != null
                && action.toLowerCase().contains("traffic_light")
                || TrafficLightParser.hasTrafficLightPayload(intent.getExtras()));
        if (trafficInput) {
            Bundle extras = intent.getExtras();
            Log.d(TAG, "Traffic input action=" + action
                    + " KEY_TYPE=" + BundleReaders.intValue(extras, -1, "KEY_TYPE", "keyType")
                    + " extras=" + TrafficLightParser.describeExtras(extras));
        }
        boolean forceClear = intent != null && TrafficLightParser.isClearPayload(intent.getExtras());
        Esp32NavState snapshot = aggregator.handleIntent(intent);
        if (trafficInput) {
            AppSettings.noteTrafficDiagnostic(this, "action=" + action
                    + " lights=" + snapshot.lights.size()
                    + " extras=" + TrafficLightParser.describeExtras(intent.getExtras()));
        }
        forwarder.send(withMusic(snapshot), forceClear);
    }

    private Esp32NavState withMusic(Esp32NavState snapshot) {
        MusicStateStore.copyInto(snapshot.music);
        return snapshot;
    }

    private void registerAmapReceiver() {
        receiver = new AMapBroadcastReceiver(this);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(receiver, AMapBroadcastReceiver.createFilter(), Context.RECEIVER_EXPORTED);
        } else {
            registerReceiver(receiver, AMapBroadcastReceiver.createFilter());
        }
    }

    private void requestAmapSnapshots(boolean includeExit) {
        requestAmapSnapshot(AMapConstants.KEY_TYPE_REQUEST_LANE, 0);
        requestAmapSnapshot(AMapConstants.KEY_TYPE_TRAFFIC_LIGHT, 0);
        requestAmapSnapshot(AMapConstants.KEY_TYPE_TMC, 0);
        if (includeExit) {
            requestAmapSnapshot(AMapConstants.KEY_TYPE_EXIT_INFO, 1);
        }
    }

    private void requestAmapSnapshot(int keyType, int exitInfoType) {
        try {
            String targetPackage = AppSettings.getTargetPackage(this);
            Intent request = new Intent(AMapConstants.ACTION_RECV);
            request.setPackage(targetPackage);
            request.putExtra("KEY_TYPE", keyType);
            if (exitInfoType != 0) {
                request.putExtra("EXIT_INFO_TYPE", exitInfoType);
            }
            sendBroadcast(request);
            Log.d(TAG, "request AMap snapshot KEY_TYPE=" + keyType
                    + " targetPackage=" + targetPackage);
        } catch (Throwable error) {
            Log.w(TAG, "request AMap snapshot failed KEY_TYPE=" + keyType, error);
        }
    }

    private void acquireBackgroundLocks() {
        try {
            PowerManager power = (PowerManager) getSystemService(POWER_SERVICE);
            if (power != null) {
                wakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,
                        getPackageName() + ":forwarder");
                wakeLock.setReferenceCounted(false);
                wakeLock.acquire();
            }
        } catch (Throwable error) {
            Log.w(TAG, "Unable to acquire CPU wake lock", error);
        }
        try {
            WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
            if (wifi != null) {
                wifiLock = wifi.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF,
                        getPackageName() + ":udp");
                wifiLock.setReferenceCounted(false);
                wifiLock.acquire();
            }
        } catch (Throwable error) {
            Log.w(TAG, "Unable to acquire Wi-Fi lock", error);
        }
    }

    private void releaseBackgroundLocks() {
        try {
            if (wifiLock != null && wifiLock.isHeld()) {
                wifiLock.release();
            }
        } catch (Throwable ignored) {
        }
        wifiLock = null;
        try {
            if (wakeLock != null && wakeLock.isHeld()) {
                wakeLock.release();
            }
        } catch (Throwable ignored) {
        }
        wakeLock = null;
    }

    private Notification buildNotification() {
        NotificationManager manager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && manager != null) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "AMap ESP32 Forwarder",
                NotificationManager.IMPORTANCE_DEFAULT);
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
