package com.zuoqirun.amapesp32forwarder;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.BatteryManager;
import android.os.Build;
import android.provider.CalendarContract;
import android.telephony.TelephonyManager;
import android.util.Log;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Collects non-navigation phone state without adding an external dependency. */
final class PhoneCompanionManager {
    private static final String TAG = "PhoneCompanion";
    private static final long CALENDAR_INTERVAL_MS = 5 * 60_000L;
    private static final long LOCATION_INTERVAL_MS = 30 * 60_000L;
    private static final long WEATHER_INTERVAL_MS = 15 * 60_000L;

    private final Context context;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private long lastCalendarAt;
    private long lastLocationAt;
    private long lastWeatherAt;
    private boolean weatherPending;
    private Location lastLocation;

    PhoneCompanionManager(Context context) { this.context = context.getApplicationContext(); }

    void refresh() {
        if (!AppSettings.isPhoneEnabled(context)) return;
        long now = System.currentTimeMillis();
        refreshDevice();
        if (AppSettings.isPhoneCalendarEnabled(context) && now - lastCalendarAt >= CALENDAR_INTERVAL_MS) {
            lastCalendarAt = now;
            refreshCalendar(now);
        }
        if (AppSettings.isPhoneWeatherEnabled(context)) {
            if (now - lastLocationAt >= LOCATION_INTERVAL_MS) {
                lastLocationAt = now;
                refreshLocation();
            }
            if (lastLocation != null && now - lastWeatherAt >= WEATHER_INTERVAL_MS && !weatherPending) {
                lastWeatherAt = now;
                weatherPending = true;
                Location location = lastLocation;
                executor.execute(() -> refreshWeather(location));
            }
        }
    }

    void shutdown() { executor.shutdownNow(); }

    private void refreshDevice() {
        Esp32NavState.Device device = new Esp32NavState.Device();
        BatteryManager battery = (BatteryManager) context.getSystemService(Context.BATTERY_SERVICE);
        if (battery != null) {
            device.batteryPercent = battery.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY);
            Intent batteryStatus = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            int status = batteryStatus == null ? -1
                    : batteryStatus.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
            device.charging = status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL;
        }
        BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        device.bluetoothOn = adapter != null && adapter.isEnabled();
        ConnectivityManager connectivity = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (connectivity != null) {
            Network active = connectivity.getActiveNetwork();
            NetworkCapabilities caps = active == null ? null : connectivity.getNetworkCapabilities(active);
            if (caps != null) device.network = caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ? "wifi"
                    : caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) ? "cellular" : "other";
        }
        if (context.checkSelfPermission(Manifest.permission.READ_PHONE_STATE) == PackageManager.PERMISSION_GRANTED) {
            TelephonyManager telephony = (TelephonyManager) context.getSystemService(Context.TELEPHONY_SERVICE);
            if (telephony != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && telephony.getSignalStrength() != null) {
                device.signalLevel = telephony.getSignalStrength().getLevel();
            }
        }
        PhoneStateStore.updateDevice(device);
    }

    private void refreshCalendar(long now) {
        if (context.checkSelfPermission(Manifest.permission.READ_CALENDAR) != PackageManager.PERMISSION_GRANTED) return;
        String[] projection = {CalendarContract.Instances.TITLE, CalendarContract.Instances.EVENT_LOCATION,
                CalendarContract.Instances.BEGIN, CalendarContract.Instances.END};
        Cursor cursor = null;
        try {
            cursor = CalendarContract.Instances.query(context.getContentResolver(), projection, now, now + 24 * 60 * 60_000L);
            if (cursor != null && cursor.moveToFirst()) {
                PhoneStateStore.updateCalendar(cursor.getString(0), cursor.getString(1), cursor.getLong(2), cursor.getLong(3));
            } else PhoneStateStore.updateCalendar("", "", -1L, -1L);
        } catch (SecurityException error) { Log.w(TAG, "Calendar access denied", error); }
        finally { if (cursor != null) cursor.close(); }
    }

    private void refreshLocation() {
        if (context.checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) != PackageManager.PERMISSION_GRANTED
                && context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) return;
        LocationManager manager = (LocationManager) context.getSystemService(Context.LOCATION_SERVICE);
        if (manager == null) return;
        try {
            Location network = manager.getLastKnownLocation(LocationManager.NETWORK_PROVIDER);
            Location gps = manager.getLastKnownLocation(LocationManager.GPS_PROVIDER);
            lastLocation = newer(network, gps);
            manager.requestSingleUpdate(LocationManager.PASSIVE_PROVIDER, new LocationListener() {
                @Override public void onLocationChanged(Location location) { lastLocation = location; }
            }, null);
        } catch (SecurityException | IllegalArgumentException error) { Log.w(TAG, "Location unavailable", error); }
    }

    private void refreshWeather(Location location) {
        Esp32NavState.Weather result = new Esp32NavState.Weather();
        result.provider = AppSettings.getWeatherProvider(context);
        try {
            if (AppSettings.WEATHER_QWEATHER.equals(result.provider)) fetchQWeather(location, result);
            else fetchOpenMeteo(location, result);
            result.observedAt = System.currentTimeMillis();
        } catch (Throwable error) {
            result.error = PhoneStateStore.limit(error.getMessage(), 64);
            Log.w(TAG, "Weather refresh failed", error);
        } finally {
            PhoneStateStore.updateWeather(result);
            weatherPending = false;
        }
    }

    private void fetchOpenMeteo(Location location, Esp32NavState.Weather out) throws Exception {
        String coordinates = "latitude=" + location.getLatitude() + "&longitude=" + location.getLongitude();
        JSONObject weather = getJson("https://api.open-meteo.com/v1/forecast?" + coordinates
                + "&current=temperature_2m,weather_code,precipitation&timezone=auto", null);
        JSONObject current = weather.getJSONObject("current");
        out.temperatureC = current.optDouble("temperature_2m", Double.NaN);
        out.precipitationMm = current.optDouble("precipitation", Double.NaN);
        out.code = current.optInt("weather_code", -1);
        out.condition = weatherName(out.code);
        JSONObject air = getJson("https://air-quality-api.open-meteo.com/v1/air-quality?" + coordinates + "&current=us_aqi", null);
        out.aqi = air.optJSONObject("current") == null ? -1 : air.getJSONObject("current").optInt("us_aqi", -1);
    }

    private void fetchQWeather(Location location, Esp32NavState.Weather out) throws Exception {
        String host = AppSettings.getQWeatherHost(context);
        String token = AppSettings.getQWeatherToken(context);
        if (host.isEmpty() || token.isEmpty()) throw new IllegalStateException("请填写 QWeather Host 和 Token");
        String point = URLEncoder.encode(location.getLongitude() + "," + location.getLatitude(), "UTF-8");
        JSONObject root = getJson(host + "/v7/weather/now?location=" + point + "&lang=zh", "Bearer " + token);
        JSONObject now = root.getJSONObject("now");
        out.temperatureC = now.optDouble("temp", Double.NaN);
        out.precipitationMm = now.optDouble("precip", Double.NaN);
        out.condition = now.optString("text", "");
        out.code = now.optInt("icon", -1);
        try {
            JSONObject warnings = getJson(host + "/v7/warning/now?location=" + point + "&lang=zh", "Bearer " + token);
            org.json.JSONArray entries = warnings.optJSONArray("warning");
            if (entries != null && entries.length() > 0) {
                out.alert = entries.optJSONObject(0) == null ? "" : entries.optJSONObject(0).optString("title", "");
            }
        } catch (Throwable warningError) {
            Log.w(TAG, "QWeather warning refresh failed", warningError);
        }
    }

    private static JSONObject getJson(String url, String authorization) throws Exception {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(10_000); connection.setReadTimeout(10_000);
        connection.setRequestProperty("Accept", "application/json");
        if (authorization != null) connection.setRequestProperty("Authorization", authorization);
        int code = connection.getResponseCode();
        BufferedReader reader = new BufferedReader(new InputStreamReader(code >= 200 && code < 300 ? connection.getInputStream() : connection.getErrorStream(), StandardCharsets.UTF_8));
        StringBuilder body = new StringBuilder(); String line;
        while ((line = reader.readLine()) != null) body.append(line);
        reader.close(); connection.disconnect();
        if (code < 200 || code >= 300) throw new IllegalStateException("HTTP " + code + " " + body);
        return new JSONObject(body.toString());
    }

    private static Location newer(Location left, Location right) { return left == null ? right : right == null || left.getTime() >= right.getTime() ? left : right; }
    private static String weatherName(int code) { return code == 0 ? "晴" : code <= 3 ? "多云" : code <= 48 ? "雾" : code <= 67 ? "雨" : code <= 77 ? "雪" : code <= 82 ? "阵雨" : "雷雨"; }
}
