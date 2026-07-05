package com.zuoqirun.amapesp32forwarder;

import android.os.Bundle;
import android.text.TextUtils;

import org.json.JSONArray;
import org.json.JSONObject;

import java.lang.reflect.Array;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class TrafficLightParser {
    private static final Pattern CAMERA_LIGHT_PATTERN = Pattern.compile("CameraLightInfo\\{([^}]*)\\}");

    private TrafficLightParser() {}

    static Result parse(Bundle extras, boolean inCruiseMode, int navigationTurnDir, int currentTurnIcon) {
        if (extras == null || !hasTrafficLightPayload(extras)) {
            return Result.notHandled();
        }
        if (BundleReaders.booleanValue(extras, false, "clearLights", "EXTRA_CLEAR_LIGHTS")
                || isExplicitEmptyPayload(extras)) {
            return Result.clear();
        }

        ArrayList<Esp32NavState.Light> lights = new ArrayList<>();
        boolean cruise = inCruiseMode;
        cruise |= parseLightsData(BundleReaders.safeExtra(extras, "lightsData"), lights);
        cruise |= parseLightsData(BundleReaders.safeExtra(extras, "LIGHTS_DATA"), lights);
        parseCameraLightPayloads(extras, lights);
        parseJsonPayloads(extras, lights);
        parseDirectional(extras, lights);
        parseGenericArrays(extras, lights, cruise, navigationTurnDir, currentTurnIcon);
        if (lights.isEmpty()) {
            return Result.notHandled();
        }
        return Result.data(lights, cruise);
    }

    static boolean hasTrafficLightPayload(Bundle extras) {
        if (extras == null) {
            return false;
        }
        int keyType = BundleReaders.intValue(extras, -1, "KEY_TYPE", "keyType");
        return keyType == AMapConstants.KEY_TYPE_TRAFFIC_LIGHT
                || BundleReaders.hasAny(extras,
                "trafficLightStatus", "TRAFFIC_LIGHT_STATUS", "traffic_light_status",
                "redLightCountDownSeconds", "redLightCountDownSecond", "redLightCountdownSeconds",
                "greenLightLastSecond", "greenLightCountDownSeconds", "greenLightCountdownSeconds",
                "leftRedLightCountDownSeconds", "straightRedLightCountDownSeconds",
                "rightRedLightCountDownSeconds", "leftGreenLightLastSecond",
                "straightGreenLightLastSecond", "rightGreenLightLastSecond",
                "dir", "direction", "trafficLightDir", "trafficLightDirection",
                "trafficLights", "trafficLight", "trafficLightInfo", "trafficLightsCountdownInfo",
                "lightsData", "LIGHTS_DATA", "cameraLightInfo", "cameraLightInfos",
                "cameraLights", "lightInfos", "lightsCount", "LIGHTS_COUNT",
                "clearLights", "EXTRA_CLEAR_LIGHTS");
    }

    private static boolean isExplicitEmptyPayload(Bundle extras) {
        int count = BundleReaders.intValue(extras, -1, "lightsCount", "LIGHTS_COUNT",
                "TRAFFIC_LIGHT_NUM", "routeRemainTrafficLightNum");
        return count == 0 && !BundleReaders.hasAny(extras,
                "trafficLightStatus", "redLightCountDownSeconds", "greenLightLastSecond",
                "lightsData", "LIGHTS_DATA");
    }

    private static boolean parseLightsData(Object value, List<Esp32NavState.Light> target) {
        if (value == null) {
            return false;
        }
        if (value instanceof Bundle) {
            Bundle bundle = (Bundle) value;
            putLight(target,
                    BundleReaders.intValue(bundle, -1, "dir", "direction", "c"),
                    normalizeCruiseStatus(BundleReaders.intValue(bundle, -1, "status", "trafficLightStatus", "d")),
                    BundleReaders.intValue(bundle, 0, "countdown", "countDown",
                            "redLightCountDownSeconds", "e"),
                    true);
            return true;
        }
        if (value instanceof Iterable) {
            boolean handled = false;
            for (Object item : (Iterable<?>) value) {
                handled |= parseLightsData(item, target);
            }
            return handled;
        }
        Class<?> cls = value.getClass();
        if (cls.isArray()) {
            boolean handled = false;
            int length = Array.getLength(value);
            for (int i = 0; i < length; i++) {
                handled |= parseLightsData(Array.get(value, i), target);
            }
            return handled;
        }
        String text = String.valueOf(value).trim();
        if (TextUtils.isEmpty(text)) {
            return false;
        }
        try {
            if (text.startsWith("[")) {
                JSONArray array = new JSONArray(text);
                boolean handled = false;
                for (int i = 0; i < array.length(); i++) {
                    handled |= parseLightObject(array.optJSONObject(i), target, true);
                }
                return handled;
            }
            if (text.startsWith("{")) {
                return parseLightObject(new JSONObject(text), target, true);
            }
        } catch (Throwable ignored) {
        }
        return false;
    }

    private static void parseCameraLightPayloads(Bundle extras, List<Esp32NavState.Light> target) {
        for (String key : extras.keySet()) {
            Object value = BundleReaders.safeExtra(extras, key);
            if (value == null) {
                continue;
            }
            String lowerKey = key == null ? "" : key.toLowerCase(Locale.US);
            String text = String.valueOf(value);
            if (lowerKey.contains("cameralight") || lowerKey.contains("camera_light")
                    || lowerKey.contains("lightinfos") || text.contains("CameraLightInfo{")) {
                parseCameraLightText(text, target);
            }
        }
    }

    private static void parseCameraLightText(String text, List<Esp32NavState.Light> target) {
        Matcher matcher = CAMERA_LIGHT_PATTERN.matcher(text);
        while (matcher.find()) {
            int dir = -1;
            int status = -1;
            int countDown = 0;
            for (String part : matcher.group(1).split(",")) {
                String[] pair = part.split("=", 2);
                if (pair.length != 2) {
                    continue;
                }
                String name = pair[0].trim();
                int value = BundleReaders.parseInt(pair[1].trim(), Integer.MIN_VALUE);
                if ("direction".equals(name) || "dir".equals(name) || "c".equals(name)) {
                    dir = value;
                } else if ("status".equals(name) || "trafficLightStatus".equals(name) || "d".equals(name)) {
                    status = value;
                } else if ("countDown".equals(name) || "countdown".equals(name)
                        || "redLightCountDownSeconds".equals(name) || "e".equals(name)) {
                    countDown = value;
                }
            }
            putLight(target, normalizeCruiseDirection(dir), normalizeCruiseStatus(status), countDown, true);
        }
    }

    private static void parseJsonPayloads(Bundle extras, List<Esp32NavState.Light> target) {
        for (String key : extras.keySet()) {
            Object value = BundleReaders.safeExtra(extras, key);
            if (value == null) {
                continue;
            }
            String text = String.valueOf(value).trim();
            if (!looksLikeTrafficLightJson(key, text)) {
                continue;
            }
            try {
                if (text.startsWith("[")) {
                    JSONArray array = new JSONArray(text);
                    for (int i = 0; i < array.length(); i++) {
                        parseLightObject(array.optJSONObject(i), target, false);
                    }
                } else if (text.startsWith("{")) {
                    parseLightObject(new JSONObject(text), target, false);
                }
            } catch (Throwable ignored) {
            }
        }
    }

    private static boolean looksLikeTrafficLightJson(String key, String text) {
        String lowerKey = key == null ? "" : key.toLowerCase(Locale.US);
        String lowerText = text.toLowerCase(Locale.US);
        boolean jsonShape = text.startsWith("{") || text.startsWith("[");
        return jsonShape
                && (lowerKey.contains("trafficlight") || lowerKey.contains("traffic_light")
                || lowerKey.contains("redlight") || lowerKey.contains("greenlight")
                || lowerText.contains("redlightcountdownseconds")
                || lowerText.contains("greenlightlastsecond")
                || lowerText.contains("trafficlightstatus")
                || lowerText.contains("\"countdown\""));
    }

    private static boolean parseLightObject(JSONObject object, List<Esp32NavState.Light> target, boolean cruise) {
        if (object == null) {
            return false;
        }
        boolean handled = false;
        String[] arrays = {"lightsData", "trafficLights", "trafficLight", "lights", "cameraLightInfos", "cameraLights"};
        for (String name : arrays) {
            JSONArray nested = object.optJSONArray(name);
            if (nested == null) {
                continue;
            }
            for (int i = 0; i < nested.length(); i++) {
                handled |= parseLightObject(nested.optJSONObject(i), target, cruise);
            }
        }
        int dir = object.optInt("dir", object.optInt("direction",
                object.optInt("trafficLightDir", object.optInt("trafficLightDirection", object.optInt("c", -1)))));
        int status = object.optInt("trafficLightStatus", object.optInt("status",
                object.optInt("trafficLightState", object.optInt("d", -1))));
        int red = object.optInt("redLightCountDownSeconds", object.optInt("redLightCountdownSeconds",
                object.optInt("redSeconds", object.optInt("redCountDown", 0))));
        int green = object.optInt("greenLightLastSecond", object.optInt("greenLightCountDownSeconds",
                object.optInt("greenLightCountdownSeconds", object.optInt("greenSeconds", 0))));
        int countDown = object.optInt("countdown", object.optInt("countDown", Math.max(red, green)));
        int seconds = secondsFor(status, red, green, countDown);
        if (seconds > 0) {
            putLight(target, cruise ? normalizeCruiseDirection(dir) : dir,
                    cruise ? normalizeCruiseStatus(status) : status, seconds, cruise);
            handled = true;
        }
        return handled;
    }

    private static void parseDirectional(Bundle extras, List<Esp32NavState.Light> target) {
        putDirectional(target, extras, AMapConstants.DIR_LEFT, false,
                "leftRedLightCountDownSeconds", "LEFT_RED_LIGHT_COUNT_DOWN_SECONDS", "leftRedSeconds");
        putDirectional(target, extras, AMapConstants.DIR_STRAIGHT, false,
                "straightRedLightCountDownSeconds", "STRAIGHT_RED_LIGHT_COUNT_DOWN_SECONDS", "frontRedSeconds");
        putDirectional(target, extras, AMapConstants.DIR_RIGHT, false,
                "rightRedLightCountDownSeconds", "RIGHT_RED_LIGHT_COUNT_DOWN_SECONDS", "rightRedSeconds");
        putDirectional(target, extras, AMapConstants.DIR_LEFT, true,
                "leftGreenLightLastSecond", "LEFT_GREEN_LIGHT_LAST_SECOND", "leftGreenSeconds");
        putDirectional(target, extras, AMapConstants.DIR_STRAIGHT, true,
                "straightGreenLightLastSecond", "STRAIGHT_GREEN_LIGHT_LAST_SECOND", "frontGreenSeconds");
        putDirectional(target, extras, AMapConstants.DIR_RIGHT, true,
                "rightGreenLightLastSecond", "RIGHT_GREEN_LIGHT_LAST_SECOND", "rightGreenSeconds");
    }

    private static void putDirectional(List<Esp32NavState.Light> target, Bundle extras,
                                       int dir, boolean green, String... keys) {
        int seconds = BundleReaders.intValue(extras, 0, keys);
        if (seconds > 0) {
            putLight(target, dir, green ? AMapConstants.LIGHT_STATUS_GREEN : AMapConstants.LIGHT_STATUS_RED,
                    seconds, false);
        }
    }

    private static void parseGenericArrays(Bundle extras, List<Esp32NavState.Light> target,
                                           boolean cruise, int navigationTurnDir, int currentTurnIcon) {
        int[] dirs = BundleReaders.intArrayValue(extras, "dir", "DIR", "dirs", "DIRECTIONS",
                "direction", "directions", "trafficLightDir", "trafficLightDirs",
                "trafficLightDirection", "trafficLightDirections");
        int[] statuses = BundleReaders.intArrayValue(extras, "trafficLightStatus", "TRAFFIC_LIGHT_STATUS",
                "trafficLightStatuses", "traffic_light_status", "trafficLightState", "trafficLightStates");
        int[] reds = BundleReaders.intArrayValue(extras, "redLightCountDownSeconds",
                "redLightCountDownSecond", "redLightCountdownSeconds", "redSeconds", "redCountDown");
        int[] greens = BundleReaders.intArrayValue(extras, "greenLightLastSecond",
                "greenLightCountDownSeconds", "greenLightCountdownSeconds", "greenSeconds", "greenCountDown");
        int count = Math.max(Math.max(lengthOf(dirs), lengthOf(statuses)), Math.max(lengthOf(reds), lengthOf(greens)));
        if (count == 0 && BundleReaders.hasAny(extras, "trafficLightStatus", "redLightCountDownSeconds",
                "greenLightLastSecond")) {
            count = 1;
        }
        for (int i = 0; i < count; i++) {
            int dir = valueAt(dirs, i, BundleReaders.intValue(extras, -1, "dir", "direction"));
            int status = valueAt(statuses, i, BundleReaders.intValue(extras, -1, "trafficLightStatus"));
            int red = valueAt(reds, i, BundleReaders.intValue(extras, 0, "redLightCountDownSeconds"));
            int green = valueAt(greens, i, BundleReaders.intValue(extras, 0, "greenLightLastSecond"));
            int seconds = secondsFor(status, red, green, 0);
            if (seconds > 0) {
                if (!cruise) {
                    dir = normalizeNavDirection(dir, status, red, green, navigationTurnDir, currentTurnIcon);
                }
                putLight(target, dir, status, seconds, cruise);
            }
        }
    }

    private static void putLight(List<Esp32NavState.Light> target, int dir, int status, int seconds, boolean cruise) {
        if (seconds <= 0) {
            return;
        }
        Esp32NavState.Light light = new Esp32NavState.Light();
        light.dir = dir;
        light.status = status;
        light.seconds = Math.min(seconds, 999);
        light.updatedAt = System.currentTimeMillis();
        light.ttlMs = cruise ? seconds * 1000L + 2000L : 4500L;
        replaceByDir(target, light);
    }

    private static void replaceByDir(List<Esp32NavState.Light> target, Esp32NavState.Light light) {
        for (int i = 0; i < target.size(); i++) {
            Esp32NavState.Light old = target.get(i);
            if (old.dir == light.dir) {
                if (light.status == AMapConstants.LIGHT_STATUS_RED || old.status != AMapConstants.LIGHT_STATUS_RED) {
                    target.set(i, light);
                }
                return;
            }
        }
        if (target.size() < 4) {
            target.add(light);
        }
    }

    private static int secondsFor(int status, int red, int green, int countDown) {
        if (countDown > 0) {
            return countDown;
        }
        if (status == AMapConstants.LIGHT_STATUS_GREEN) {
            return green > 0 ? green : red;
        }
        return red > 0 ? red : green;
    }

    private static int normalizeCruiseStatus(int status) {
        if (status == 0) {
            return AMapConstants.LIGHT_STATUS_RED;
        }
        if (status == 1 || status == 2 || status == 4) {
            return AMapConstants.LIGHT_STATUS_GREEN;
        }
        return status;
    }

    private static int normalizeCruiseDirection(int dir) {
        if (dir == 2) {
            return AMapConstants.DIR_STRAIGHT;
        }
        if (dir == 3) {
            return AMapConstants.DIR_RIGHT;
        }
        return dir;
    }

    private static int normalizeNavDirection(int dir, int status, int red, int green,
                                             int navigationTurnDir, int currentTurnIcon) {
        if (status != AMapConstants.LIGHT_STATUS_RED && red <= 0 && green > 0) {
            return dir;
        }
        int effective = navigationTurnDir >= 0 ? navigationTurnDir : turnIconToTrafficDir(currentTurnIcon);
        if (dir == AMapConstants.DIR_RIGHT_ALT && effective == AMapConstants.DIR_UTURN) {
            return AMapConstants.DIR_UTURN;
        }
        if (dir == AMapConstants.DIR_UTURN) {
            return AMapConstants.DIR_STRAIGHT;
        }
        return dir;
    }

    private static int turnIconToTrafficDir(int icon) {
        if (icon == 2 || icon == 4 || icon == 6 || icon == 18) {
            return AMapConstants.DIR_LEFT;
        }
        if (icon == 3 || icon == 5 || icon == 7 || icon == 19) {
            return AMapConstants.DIR_RIGHT;
        }
        if (icon == 8) {
            return AMapConstants.DIR_UTURN;
        }
        return AMapConstants.DIR_STRAIGHT;
    }

    private static int lengthOf(int[] values) {
        return values == null ? 0 : values.length;
    }

    private static int valueAt(int[] values, int index, int fallback) {
        if (values == null || values.length == 0) {
            return fallback;
        }
        return index < values.length ? values[index] : values[values.length - 1];
    }

    static final class Result {
        final boolean handled;
        final boolean clear;
        final boolean setCruiseMode;
        final List<Esp32NavState.Light> lights;

        private Result(boolean handled, boolean clear, boolean setCruiseMode, List<Esp32NavState.Light> lights) {
            this.handled = handled;
            this.clear = clear;
            this.setCruiseMode = setCruiseMode;
            this.lights = lights;
        }

        static Result notHandled() {
            return new Result(false, false, false, new ArrayList<>());
        }

        static Result clear() {
            return new Result(true, true, false, new ArrayList<>());
        }

        static Result data(List<Esp32NavState.Light> lights, boolean cruiseMode) {
            return new Result(true, false, cruiseMode, lights);
        }
    }
}
