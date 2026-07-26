package com.zuoqirun.amapesp32forwarder;

import android.app.Notification;
import android.os.Bundle;
import android.service.notification.StatusBarNotification;
import android.text.TextUtils;

/** Thread-safe state shared by notification callbacks and the foreground forwarder. */
final class PhoneStateStore {
    private static final Object LOCK = new Object();
    private static final Esp32NavState.Phone STATE = new Esp32NavState.Phone();
    static final String WECHAT_PACKAGE = "com.tencent.mm";
    static final String QQ_PACKAGE = "com.tencent.mobileqq";

    private PhoneStateStore() {}

    static void copyInto(Esp32NavState.Phone target, boolean enabled) {
        synchronized (LOCK) {
            target.copyFrom(STATE);
            target.enabled = enabled;
        }
    }

    static boolean updateNotification(StatusBarNotification sbn) {
        if (sbn == null) return false;
        Notification notification = sbn.getNotification();
        if (notification == null) return false;
        String packageName = sbn.getPackageName();
        boolean call = Notification.CATEGORY_CALL.equals(notification.category);
        if (!call && !WECHAT_PACKAGE.equals(packageName) && !QQ_PACKAGE.equals(packageName)) return false;
        Bundle extras = notification.extras;
        String title = text(extras, Notification.EXTRA_TITLE);
        String body = text(extras, Notification.EXTRA_BIG_TEXT);
        if (TextUtils.isEmpty(body)) body = text(extras, Notification.EXTRA_TEXT);
        if (TextUtils.isEmpty(body)) body = text(extras, Notification.EXTRA_SUMMARY_TEXT);
        synchronized (LOCK) {
            STATE.notification.active = true;
            STATE.notification.kind = call ? "call" : "message";
            STATE.notification.app = call ? "电话" : (WECHAT_PACKAGE.equals(packageName) ? "微信" : "QQ");
            STATE.notification.sender = limit(title, 64);
            STATE.notification.title = limit(title, 96);
            STATE.notification.body = limit(body, 512);
            STATE.notification.postedAt = sbn.getPostTime();
            STATE.notification.expiresAt = call ? Long.MAX_VALUE : System.currentTimeMillis() + 6000L;
        }
        return true;
    }

    static boolean clearNotification(StatusBarNotification sbn) {
        if (sbn == null) return false;
        synchronized (LOCK) {
            if (STATE.notification.active && "call".equals(STATE.notification.kind)
                    && Notification.CATEGORY_CALL.equals(sbn.getNotification().category)) {
                STATE.notification.active = false;
                STATE.notification.expiresAt = 0L;
                return true;
            }
        }
        return false;
    }

    static void updateCalendar(String title, String location, long startAt, long endAt) {
        synchronized (LOCK) { STATE.calendar.title = limit(title, 96); STATE.calendar.location = limit(location, 96); STATE.calendar.startAt = startAt; STATE.calendar.endAt = endAt; }
    }
    static void updateWeather(Esp32NavState.Weather weather) { synchronized (LOCK) { STATE.weather.copyFrom(weather); } }
    static void updateDevice(Esp32NavState.Device device) { synchronized (LOCK) { STATE.device.copyFrom(device); } }

    private static String text(Bundle extras, String key) {
        if (extras == null) return "";
        CharSequence value = extras.getCharSequence(key);
        return value == null ? "" : value.toString().trim();
    }
    static String limit(String input, int max) {
        if (input == null) return "";
        String value = input.trim();
        return value.codePointCount(0, value.length()) <= max ? value
                : value.substring(0, value.offsetByCodePoints(0, max));
    }
}
