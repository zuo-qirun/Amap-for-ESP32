package com.zuoqirun.amapesp32forwarder;

import android.content.Context;
import android.content.SharedPreferences;

final class AppSettings {
    static final String TRANSPORT_UDP = "udp";
    static final String TRANSPORT_BLE = "ble";

    private static final String PREFS = "amap_esp32_forwarder";
    private static final String KEY_ENABLED = "enabled";
    private static final String KEY_TRANSPORT = "transport";
    private static final String KEY_IP = "esp32_ip";
    private static final String KEY_PORT = "udp_port";
    private static final String KEY_LAST_SENT = "last_sent";
    private static final String KEY_LAST_PAYLOAD = "last_payload";
    private static final String KEY_LAST_ERROR = "last_error";
    private static final String KEY_LAST_BROADCAST = "last_broadcast";

    private AppSettings() {}

    static SharedPreferences prefs(Context context) {
        return context.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    static boolean isEnabled(Context context) {
        return prefs(context).getBoolean(KEY_ENABLED, false);
    }

    static void setEnabled(Context context, boolean enabled) {
        prefs(context).edit().putBoolean(KEY_ENABLED, enabled).apply();
    }

    static String getTransport(Context context) {
        return prefs(context).getString(KEY_TRANSPORT, TRANSPORT_UDP);
    }

    static void setTransport(Context context, String transport) {
        prefs(context).edit().putString(KEY_TRANSPORT, transport).apply();
    }

    static String getEsp32Ip(Context context) {
        return prefs(context).getString(KEY_IP, "192.168.4.2");
    }

    static void setEsp32Ip(Context context, String ip) {
        prefs(context).edit().putString(KEY_IP, ip == null ? "" : ip.trim()).apply();
    }

    static int getUdpPort(Context context) {
        return prefs(context).getInt(KEY_PORT, 4210);
    }

    static void setUdpPort(Context context, int port) {
        prefs(context).edit().putInt(KEY_PORT, Math.max(1, Math.min(65535, port))).apply();
    }

    static void noteBroadcast(Context context) {
        prefs(context).edit().putLong(KEY_LAST_BROADCAST, System.currentTimeMillis()).apply();
    }

    static long getLastBroadcast(Context context) {
        return prefs(context).getLong(KEY_LAST_BROADCAST, 0L);
    }

    static void noteSent(Context context, int payloadBytes) {
        prefs(context).edit()
                .putLong(KEY_LAST_SENT, System.currentTimeMillis())
                .putInt(KEY_LAST_PAYLOAD, payloadBytes)
                .putString(KEY_LAST_ERROR, "")
                .apply();
    }

    static long getLastSent(Context context) {
        return prefs(context).getLong(KEY_LAST_SENT, 0L);
    }

    static int getLastPayloadBytes(Context context) {
        return prefs(context).getInt(KEY_LAST_PAYLOAD, 0);
    }

    static void noteError(Context context, String error) {
        prefs(context).edit().putString(KEY_LAST_ERROR, error == null ? "" : error).apply();
    }

    static String getLastError(Context context) {
        return prefs(context).getString(KEY_LAST_ERROR, "");
    }
}
