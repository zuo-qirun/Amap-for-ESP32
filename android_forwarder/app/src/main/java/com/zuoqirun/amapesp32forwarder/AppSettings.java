package com.zuoqirun.amapesp32forwarder;

import android.content.Context;
import android.content.SharedPreferences;

final class AppSettings {
    static final String TRANSPORT_UDP = "udp";
    static final String TRANSPORT_BLE = "ble";
    static final String DEFAULT_TARGET_PACKAGE = "com.autonavi.amapauto";

    private static final String PREFS = "amap_esp32_forwarder";
    private static final String KEY_ENABLED = "enabled";
    private static final String KEY_TRANSPORT = "transport";
    private static final String KEY_IP = "esp32_ip";
    private static final String KEY_PORT = "udp_port";
    private static final String KEY_TARGET_PACKAGE = "target_package";
    private static final String KEY_LYRIC_OFFSET_MS = "lyric_offset_ms";
    private static final String KEY_LAST_SENT = "last_sent";
    private static final String KEY_LAST_PAYLOAD = "last_payload";
    private static final String KEY_LAST_ERROR = "last_error";
    private static final String KEY_LAST_BROADCAST = "last_broadcast";
    private static final String KEY_LAST_TRAFFIC_DIAGNOSTIC = "last_traffic_diagnostic";
    private static final String KEY_PHONE_ENABLED = "phone_enabled";
    private static final String KEY_PHONE_NOTIFICATIONS = "phone_notifications";
    private static final String KEY_PHONE_CALENDAR = "phone_calendar";
    private static final String KEY_PHONE_WEATHER = "phone_weather";
    private static final String KEY_WEATHER_PROVIDER = "weather_provider";
    private static final String KEY_QWEATHER_HOST = "qweather_host";
    private static final String KEY_QWEATHER_TOKEN = "qweather_token";

    static final String WEATHER_OPEN_METEO = "open_meteo";
    static final String WEATHER_QWEATHER = "qweather";

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

    static String getTargetPackage(Context context) {
        return normalizeTargetPackage(prefs(context).getString(
                KEY_TARGET_PACKAGE, DEFAULT_TARGET_PACKAGE));
    }

    static void setTargetPackage(Context context, String packageName) {
        prefs(context).edit()
                .putString(KEY_TARGET_PACKAGE, normalizeTargetPackage(packageName))
                .apply();
    }

    static int getLyricOffsetMs(Context context) {
        return prefs(context).getInt(KEY_LYRIC_OFFSET_MS, 0);
    }

    static void setLyricOffsetMs(Context context, int offsetMs) {
        prefs(context).edit().putInt(KEY_LYRIC_OFFSET_MS,
                Math.max(-5000, Math.min(5000, offsetMs))).apply();
    }

    static String normalizeTargetPackage(String packageName) {
        if (packageName == null || packageName.trim().isEmpty()) {
            return DEFAULT_TARGET_PACKAGE;
        }
        return packageName.trim();
    }

    static void noteBroadcast(Context context) {
        prefs(context).edit().putLong(KEY_LAST_BROADCAST, System.currentTimeMillis()).apply();
    }

    static long getLastBroadcast(Context context) {
        return prefs(context).getLong(KEY_LAST_BROADCAST, 0L);
    }

    static void noteTrafficDiagnostic(Context context, String diagnostic) {
        prefs(context).edit().putString(KEY_LAST_TRAFFIC_DIAGNOSTIC,
                diagnostic == null ? "" : diagnostic).apply();
    }

    static String getLastTrafficDiagnostic(Context context) {
        return prefs(context).getString(KEY_LAST_TRAFFIC_DIAGNOSTIC, "");
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

    static boolean isPhoneEnabled(Context context) { return prefs(context).getBoolean(KEY_PHONE_ENABLED, false); }
    static void setPhoneEnabled(Context context, boolean enabled) { prefs(context).edit().putBoolean(KEY_PHONE_ENABLED, enabled).apply(); }
    static boolean arePhoneNotificationsEnabled(Context context) { return prefs(context).getBoolean(KEY_PHONE_NOTIFICATIONS, false); }
    static void setPhoneNotificationsEnabled(Context context, boolean enabled) { prefs(context).edit().putBoolean(KEY_PHONE_NOTIFICATIONS, enabled).apply(); }
    static boolean isPhoneCalendarEnabled(Context context) { return prefs(context).getBoolean(KEY_PHONE_CALENDAR, false); }
    static void setPhoneCalendarEnabled(Context context, boolean enabled) { prefs(context).edit().putBoolean(KEY_PHONE_CALENDAR, enabled).apply(); }
    static boolean isPhoneWeatherEnabled(Context context) { return prefs(context).getBoolean(KEY_PHONE_WEATHER, false); }
    static void setPhoneWeatherEnabled(Context context, boolean enabled) { prefs(context).edit().putBoolean(KEY_PHONE_WEATHER, enabled).apply(); }
    static String getWeatherProvider(Context context) { return prefs(context).getString(KEY_WEATHER_PROVIDER, WEATHER_OPEN_METEO); }
    static void setWeatherProvider(Context context, String provider) { prefs(context).edit().putString(KEY_WEATHER_PROVIDER, WEATHER_QWEATHER.equals(provider) ? WEATHER_QWEATHER : WEATHER_OPEN_METEO).apply(); }
    static String getQWeatherHost(Context context) { return prefs(context).getString(KEY_QWEATHER_HOST, ""); }
    static void setQWeatherHost(Context context, String host) { prefs(context).edit().putString(KEY_QWEATHER_HOST, host == null ? "" : host.trim().replaceAll("/+$", "")).apply(); }
    static String getQWeatherToken(Context context) { return prefs(context).getString(KEY_QWEATHER_TOKEN, ""); }
    static void setQWeatherToken(Context context, String token) { prefs(context).edit().putString(KEY_QWEATHER_TOKEN, token == null ? "" : token.trim()).apply(); }
}
