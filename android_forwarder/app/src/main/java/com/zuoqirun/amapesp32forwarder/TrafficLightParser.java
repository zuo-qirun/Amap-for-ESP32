package com.zuoqirun.amapesp32forwarder;

import android.os.Bundle;

import org.json.JSONArray;
import org.json.JSONObject;

import java.lang.reflect.Array;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class TrafficLightParser {
    private static final Pattern CAMERA_LIGHT_PATTERN = Pattern.compile("CameraLightInfo\\{([^}]*)\\}");
    private static final Pattern LOOSE_LIGHT_PATTERN = Pattern.compile("\\{([^{}]+)\\}");

    private TrafficLightParser() {}

    static Result parse(Bundle extras, boolean inCruiseMode, int navigationTurnDir, int currentTurnIcon) {
        return extras == null ? Result.notHandled()
                : parse(new BundleExtras(extras), inCruiseMode, navigationTurnDir, currentTurnIcon);
    }

    static Result parse(Map<String, ?> extras, boolean inCruiseMode,
                        int navigationTurnDir, int currentTurnIcon) {
        return extras == null ? Result.notHandled()
                : parse(new MapExtras(extras), inCruiseMode, navigationTurnDir, currentTurnIcon);
    }

    private static Result parse(Extras extras, boolean inCruiseMode,
                                int navigationTurnDir, int currentTurnIcon) {
        // The caller already filters by traffic-light action or known keys.
        // Do not require a fixed extras whitelist here: several AMap cruise
        // builds use obfuscated/extended CameraLight wrapper key names, which
        // must be discovered by the generic Bundle scan below.
        if (booleanValue(extras, false, "clearLights", "EXTRA_CLEAR_LIGHTS")
                || isExplicitEmptyLightsData(extras)) {
            return Result.clear();
        }

        ArrayList<Esp32NavState.Light> lights = new ArrayList<>();
        boolean hasLightsData = hasAny(extras, "lightsData", "LIGHTS_DATA");

        // Each format is a fallback for the preceding one. In particular, the
        // first-light compatibility fields in a cruise broadcast must never be
        // merged back into an already parsed lightsData list.
        if (parseLightsData(value(extras, "lightsData"), lights)
                || parseLightsData(value(extras, "LIGHTS_DATA"), lights)) {
            return Result.data(lights, true);
        }
        if (parseCameraLightPayloads(extras, lights)) {
            return Result.data(lights, true);
        }
        if (parseJsonPayloads(extras, lights)) {
            return Result.data(lights, inCruiseMode);
        }
        if (parseDirectional(extras, lights)) {
            return Result.data(lights, inCruiseMode);
        }

        boolean fallbackCruise = inCruiseMode || hasLightsData;
        if (parseGenericArrays(extras, lights, fallbackCruise,
                navigationTurnDir, currentTurnIcon)) {
            return Result.data(lights, fallbackCruise);
        }
        if (intValue(extras, -1, "lightsCount", "LIGHTS_COUNT", "TRAFFIC_LIGHT_NUM") == 0) {
            return Result.clear();
        }
        return Result.notHandled();
    }

    static boolean hasTrafficLightPayload(Bundle extras) {
        return extras != null && hasTrafficLightPayload(new BundleExtras(extras));
    }

    static boolean isClearPayload(Bundle extras) {
        if (extras == null) {
            return false;
        }
        Extras source = new BundleExtras(extras);
        if (booleanValue(source, false, "clearLights", "EXTRA_CLEAR_LIGHTS")
                || isExplicitEmptyLightsData(source)) {
            return true;
        }
        Result result = parse(source, false, -1, 0);
        return result.handled && result.clear;
    }

    private static boolean hasTrafficLightPayload(Extras extras) {
        int keyType = intValue(extras, -1, "KEY_TYPE", "keyType");
        return keyType == AMapConstants.KEY_TYPE_TRAFFIC_LIGHT
                || hasAny(extras,
                "trafficLightStatus", "TRAFFIC_LIGHT_STATUS", "traffic_light_status",
                "redLightCountDownSeconds", "redLightCountDownSecond", "redLightCountdownSeconds",
                "greenLightLastSecond", "greenLightCountDownSeconds", "greenLightCountdownSeconds",
                "leftRedLightCountDownSeconds", "straightRedLightCountDownSeconds",
                "rightRedLightCountDownSeconds", "leftGreenLightLastSecond",
                "straightGreenLightLastSecond", "rightGreenLightLastSecond",
                "dir", "direction", "trafficLightDir", "trafficLightDirection",
                "trafficLights", "trafficLight", "trafficLightInfo", "trafficLightsCountdownInfo",
                "lightsData", "LIGHTS_DATA", "cameraLightInfo", "cameraLightInfos",
                "cameraLightInfoWrapper", "camera_light_info_wrapper",
                "cameraLights", "lightInfos", "lightsCount", "LIGHTS_COUNT",
                "clearLights", "EXTRA_CLEAR_LIGHTS");
    }

    static String describeExtras(Bundle extras) {
        if (extras == null) {
            return "{}";
        }
        StringBuilder out = new StringBuilder("{");
        for (String key : extras.keySet()) {
            if (out.length() > 1) {
                out.append(", ");
            }
            Object value = BundleReaders.safeExtra(extras, key);
            String text = String.valueOf(value);
            if (text.length() > 360) {
                text = text.substring(0, 360) + "…";
            }
            out.append(key).append('=').append(text);
            if (out.length() > 2600) {
                out.append(" …");
                break;
            }
        }
        return out.append('}').toString();
    }

    private static boolean isExplicitEmptyLightsData(Extras extras) {
        return isExplicitEmptyList(value(extras, "lightsData"))
                || isExplicitEmptyList(value(extras, "LIGHTS_DATA"));
    }

    private static boolean isExplicitEmptyList(Object value) {
        if (value == null) {
            return false;
        }
        if (value instanceof JSONArray) {
            return ((JSONArray) value).length() == 0;
        }
        if (value instanceof Bundle) {
            Bundle bundle = (Bundle) value;
            for (String key : new String[]{"lightsData", "lights", "trafficLights"}) {
                if (bundle.containsKey(key) && isExplicitEmptyList(BundleReaders.safeExtra(bundle, key))) {
                    return true;
                }
            }
            return false;
        }
        if (value instanceof Map) {
            Map<?, ?> map = (Map<?, ?>) value;
            for (String key : new String[]{"lightsData", "lights", "trafficLights"}) {
                if (map.containsKey(key) && isExplicitEmptyList(map.get(key))) {
                    return true;
                }
            }
            return false;
        }
        if (value instanceof Iterable) {
            return !((Iterable<?>) value).iterator().hasNext();
        }
        if (value.getClass().isArray()) {
            return Array.getLength(value) == 0;
        }
        String text = String.valueOf(value).trim();
        if ("[]".equals(text)) {
            return true;
        }
        try {
            if (text.startsWith("[")) {
                return new JSONArray(text).length() == 0;
            }
            if (text.startsWith("{")) {
                JSONObject object = new JSONObject(text);
                for (String key : new String[]{"lightsData", "lights", "trafficLights"}) {
                    JSONArray nested = object.optJSONArray(key);
                    if (nested != null && nested.length() == 0) {
                        return true;
                    }
                }
            }
        } catch (Throwable ignored) {
        }
        return false;
    }

    private static boolean parseLightsData(Object value, List<Esp32NavState.Light> target) {
        if (value == null) {
            return false;
        }
        if (value instanceof Bundle) {
            Bundle bundle = (Bundle) value;
            int dir = BundleReaders.intValue(bundle, -1, "dir", "direction", "lightDirection", "c");
            int status = normalizeCruiseStatus(BundleReaders.intValue(bundle, -1,
                    "status", "trafficLightStatus", "lightStatus", "lightState", "d"));
            int red = BundleReaders.intValue(bundle, 0, "redLightCountDownSeconds",
                    "redLightCountdownSeconds", "redSeconds", "redCountDown");
            int green = BundleReaders.intValue(bundle, 0, "greenLightLastSecond",
                    "greenLightCountDownSeconds", "greenLightCountdownSeconds", "greenSeconds");
            int countDown = BundleReaders.intValue(bundle, 0, "countdown", "countDown",
                    "countDownSeconds", "remainSeconds", "remainTime", "e");
            int seconds = secondsFor(status, red, green, countDown);
            boolean handled = putLight(target, normalizeCruiseDirection(dir), status, seconds, true);
            for (String key : new String[]{"lightsData", "lights", "trafficLights"}) {
                Object nested = BundleReaders.safeExtra(bundle, key);
                if (nested != null && nested != value) {
                    handled |= parseLightsData(nested, target);
                }
            }
            return handled;
        }
        if (value instanceof Map) {
            try {
                return parseLightObject(new JSONObject((Map<?, ?>) value), target, true);
            } catch (Throwable ignored) {
                return false;
            }
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
        if (text.isEmpty()) {
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
        return parseLooseLightText(text, target);
    }

    private static boolean parseCameraLightPayloads(Extras extras, List<Esp32NavState.Light> target) {
        boolean handled = false;
        for (String key : extras.keySet()) {
            Object value = value(extras, key);
            if (value == null) {
                continue;
            }
            String lowerKey = key == null ? "" : key.toLowerCase(Locale.US);
            String text = String.valueOf(value);
            if (lowerKey.contains("cameralight") || lowerKey.contains("camera_light")
                    || lowerKey.contains("lightinfos") || text.contains("CameraLightInfo{")) {
                handled |= parseCameraLightValue(value, target);
            }
        }
        return handled;
    }

    private static boolean parseCameraLightValue(Object value, List<Esp32NavState.Light> target) {
        if (value == null) {
            return false;
        }
        if (value instanceof Bundle) {
            Bundle bundle = (Bundle) value;
            int dir = BundleReaders.intValue(bundle, -1, "direction", "dir", "c");
            int status = normalizeCruiseStatus(BundleReaders.intValue(bundle, -1,
                    "status", "trafficLightStatus", "lightStatus", "d"));
            int seconds = BundleReaders.intValue(bundle, 0, "countDown", "countdown",
                    "countDownSeconds", "redLightCountDownSeconds", "remainSeconds", "e");
            boolean handled = false;
            if (dir >= 0 && seconds > 0) {
                handled = putLight(target, normalizeCruiseDirection(dir), status, seconds, true);
            }
            for (String key : bundle.keySet()) {
                Object child = BundleReaders.safeExtra(bundle, key);
                if (child != null && child != value) {
                    handled |= parseCameraLightValue(child, target);
                }
            }
            return handled;
        }
        if (value instanceof Iterable) {
            boolean handled = false;
            for (Object item : (Iterable<?>) value) {
                handled |= parseCameraLightValue(item, target);
            }
            return handled;
        }
        Class<?> cls = value.getClass();
        if (cls.isArray()) {
            boolean handled = false;
            int length = Array.getLength(value);
            for (int i = 0; i < length; i++) {
                handled |= parseCameraLightValue(Array.get(value, i), target);
            }
            return handled;
        }
        return parseCameraLightText(String.valueOf(value), target);
    }

    private static boolean parseCameraLightText(String text, List<Esp32NavState.Light> target) {
        boolean handled = false;
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
            status = normalizeCruiseStatus(status);
            if (dir >= 0 && countDown > 0) {
                handled |= putLight(target, normalizeCruiseDirection(dir), status, countDown, true);
            }
        }
        String trimmed = text == null ? "" : text.trim();
        if (trimmed.startsWith("{") || trimmed.startsWith("[")) {
            try {
                if (trimmed.startsWith("[")) {
                    JSONArray array = new JSONArray(trimmed);
                    for (int i = 0; i < array.length(); i++) {
                        handled |= parseLightObject(array.optJSONObject(i), target, true);
                    }
                } else {
                    handled |= parseLightObject(new JSONObject(trimmed), target, true);
                }
            } catch (Throwable ignored) {
            }
        }
        return handled;
    }

    private static boolean parseJsonPayloads(Extras extras, List<Esp32NavState.Light> target) {
        boolean handled = false;
        for (String key : extras.keySet()) {
            if ("lightsData".equals(key) || "LIGHTS_DATA".equals(key)) {
                continue;
            }
            Object value = value(extras, key);
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
                        handled |= parseLightObject(array.optJSONObject(i), target, false);
                    }
                } else if (text.startsWith("{")) {
                    handled |= parseLightObject(new JSONObject(text), target, false);
                }
            } catch (Throwable ignored) {
            }
        }
        return handled;
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
        String[] arrays = {"lightsData", "trafficLights", "trafficLight", "lights", "lightList",
                "trafficLightList", "lightInfos", "cameraLightInfos", "cameraLights", "data", "items"};
        for (String name : arrays) {
            JSONArray nested = object.optJSONArray(name);
            if (nested != null) {
                for (int i = 0; i < nested.length(); i++) {
                    handled |= parseLightObject(nested.optJSONObject(i), target, cruise);
                }
            }
            JSONObject nestedObject = object.optJSONObject(name);
            if (nestedObject != null && nestedObject != object) {
                handled |= parseLightObject(nestedObject, target, cruise);
            }
        }
        int dir = jsonInt(object, -1, "dir", "direction", "lightDirection", "trafficLightDir",
                "trafficLightDirection", "c");
        int status = jsonStatus(object, -1, "trafficLightStatus", "status", "lightStatus",
                "lightState", "trafficLightState", "color", "d");
        int red = jsonInt(object, 0, "redLightCountDownSeconds", "redLightCountdownSeconds",
                "redSeconds", "redCountDown");
        int green = jsonInt(object, 0, "greenLightLastSecond", "greenLightCountDownSeconds",
                "greenLightCountdownSeconds", "greenSeconds", "greenCountDown");
        int countDown = jsonInt(object, 0, "countdown", "countDown",
                "countDownSeconds", "remainSeconds", "remainTime", "leftSeconds", "e");
        int seconds = secondsFor(status, red, green, countDown);
        if (seconds > 0) {
            if (cruise) {
                status = normalizeCruiseStatus(status);
                dir = normalizeCruiseDirection(dir);
            } else {
                if (status < 0) {
                    status = green > 0 ? AMapConstants.LIGHT_STATUS_GREEN
                            : red > 0 ? AMapConstants.LIGHT_STATUS_RED : -1;
                }
                status = displayStatus(status, red, green, seconds);
            }
            handled |= putLight(target, dir, status, seconds, cruise);
        }
        return handled;
    }

    private static boolean parseLooseLightText(String text, List<Esp32NavState.Light> target) {
        boolean handled = false;
        Matcher matcher = LOOSE_LIGHT_PATTERN.matcher(text);
        while (matcher.find()) {
            String body = matcher.group(1);
            int dir = looseValue(body, -1, "dir", "direction", "lightdirection", "c");
            int status = looseStatus(body, -1, "status", "lightstatus", "lightstate", "color", "d");
            int seconds = looseValue(body, 0, "countdown", "countdownseconds", "remainseconds",
                    "remaintime", "redlightcountdownseconds", "e");
            if (seconds > 0) {
                status = normalizeCruiseStatus(status);
                handled |= putLight(target, normalizeCruiseDirection(dir), status, seconds, true);
            }
        }
        return handled;
    }

    private static int jsonInt(JSONObject object, int fallback, String... names) {
        for (String name : names) {
            if (object.has(name)) {
                return BundleReaders.parseInt(String.valueOf(object.opt(name)), fallback);
            }
        }
        for (java.util.Iterator<String> keys = object.keys(); keys.hasNext();) {
            String key = keys.next();
            if (matchesName(key, names)) {
                return BundleReaders.parseInt(String.valueOf(object.opt(key)), fallback);
            }
        }
        return fallback;
    }

    private static int jsonStatus(JSONObject object, int fallback, String... names) {
        for (String name : names) {
            if (object.has(name)) {
                return parseStatus(String.valueOf(object.opt(name)), fallback);
            }
        }
        for (java.util.Iterator<String> keys = object.keys(); keys.hasNext();) {
            String key = keys.next();
            if (matchesName(key, names)) {
                return parseStatus(String.valueOf(object.opt(key)), fallback);
            }
        }
        return fallback;
    }

    private static int looseValue(String body, int fallback, String... names) {
        for (String part : body.split("[,;]")) {
            String[] pair = part.split("[=:]", 2);
            if (pair.length == 2 && matchesName(pair[0], names)) {
                return BundleReaders.parseInt(pair[1].replace("\"", "").trim(), fallback);
            }
        }
        return fallback;
    }

    private static int looseStatus(String body, int fallback, String... names) {
        for (String part : body.split("[,;]")) {
            String[] pair = part.split("[=:]", 2);
            if (pair.length == 2 && matchesName(pair[0], names)) {
                return parseStatus(pair[1].replace("\"", "").trim(), fallback);
            }
        }
        return fallback;
    }

    private static int parseStatus(String value, int fallback) {
        String text = value == null ? "" : value.trim().toLowerCase(Locale.US);
        if (text.contains("red")) return AMapConstants.LIGHT_STATUS_RED;
        if (text.contains("green")) return AMapConstants.LIGHT_STATUS_GREEN;
        return BundleReaders.parseInt(text, fallback);
    }

    private static boolean matchesName(String key, String... names) {
        String normalized = key == null ? "" : key.replaceAll("[^A-Za-z0-9]", "").toLowerCase(Locale.US);
        for (String name : names) {
            if (normalized.equals(name.toLowerCase(Locale.US))) {
                return true;
            }
        }
        return false;
    }

    private static boolean parseDirectional(Extras extras, List<Esp32NavState.Light> target) {
        boolean handled = false;
        handled |= putDirectional(target, extras, AMapConstants.DIR_LEFT, false,
                "leftRedLightCountDownSeconds", "LEFT_RED_LIGHT_COUNT_DOWN_SECONDS", "leftRedSeconds");
        handled |= putDirectional(target, extras, AMapConstants.DIR_STRAIGHT, false,
                "straightRedLightCountDownSeconds", "STRAIGHT_RED_LIGHT_COUNT_DOWN_SECONDS", "frontRedSeconds");
        handled |= putDirectional(target, extras, AMapConstants.DIR_RIGHT, false,
                "rightRedLightCountDownSeconds", "RIGHT_RED_LIGHT_COUNT_DOWN_SECONDS", "rightRedSeconds");
        handled |= putDirectional(target, extras, AMapConstants.DIR_LEFT, true,
                "leftGreenLightLastSecond", "LEFT_GREEN_LIGHT_LAST_SECOND", "leftGreenSeconds");
        handled |= putDirectional(target, extras, AMapConstants.DIR_STRAIGHT, true,
                "straightGreenLightLastSecond", "STRAIGHT_GREEN_LIGHT_LAST_SECOND", "frontGreenSeconds");
        handled |= putDirectional(target, extras, AMapConstants.DIR_RIGHT, true,
                "rightGreenLightLastSecond", "RIGHT_GREEN_LIGHT_LAST_SECOND", "rightGreenSeconds");
        return handled;
    }

    private static boolean putDirectional(List<Esp32NavState.Light> target, Extras extras,
                                          int dir, boolean green, String... keys) {
        int seconds = intValue(extras, 0, keys);
        if (seconds > 0) {
            int status = green ? AMapConstants.LIGHT_STATUS_GREEN : AMapConstants.LIGHT_STATUS_RED;
            return putLight(target, dir,
                    displayStatus(status, green ? 0 : seconds, green ? seconds : 0, seconds),
                    seconds, false);
        }
        return false;
    }

    private static boolean parseGenericArrays(Extras extras, List<Esp32NavState.Light> target,
                                              boolean cruise, int navigationTurnDir, int currentTurnIcon) {
        int[] dirs = intArrayValue(extras, "dir", "DIR", "dirs", "DIRECTIONS",
                "direction", "directions", "trafficLightDir", "trafficLightDirs",
                "trafficLightDirection", "trafficLightDirections");
        int[] statuses = intArrayValue(extras, "trafficLightStatus", "TRAFFIC_LIGHT_STATUS",
                "trafficLightStatuses", "traffic_light_status", "trafficLightState", "trafficLightStates");
        int[] reds = intArrayValue(extras, "redLightCountDownSeconds",
                "redLightCountDownSecond", "redLightCountdownSeconds", "redSeconds", "redCountDown");
        int[] greens = intArrayValue(extras, "greenLightLastSecond",
                "greenLightCountDownSeconds", "greenLightCountdownSeconds", "greenSeconds", "greenCountDown");
        int count = Math.max(Math.max(lengthOf(dirs), lengthOf(statuses)), Math.max(lengthOf(reds), lengthOf(greens)));
        if (count == 0 && hasAny(extras, "trafficLightStatus", "redLightCountDownSeconds",
                "greenLightLastSecond")) {
            count = 1;
        }
        boolean handled = false;
        for (int i = 0; i < count; i++) {
            int dir = valueAt(dirs, i, intValue(extras, -1, "dir", "direction"));
            int status = valueAt(statuses, i, intValue(extras, -1, "trafficLightStatus"));
            int red = valueAt(reds, i, intValue(extras, 0, "redLightCountDownSeconds"));
            int green = valueAt(greens, i, intValue(extras, 0, "greenLightLastSecond"));
            int seconds = secondsFor(status, red, green, 0);
            if (seconds > 0) {
                if (cruise) {
                    dir = normalizeCruiseDirection(dir);
                    status = normalizeCruiseStatus(status);
                } else {
                    dir = normalizeNavDirection(dir, status, red, green, navigationTurnDir, currentTurnIcon);
                    status = displayStatus(status, red, green, seconds);
                }
                handled |= putLight(target, dir, status, seconds, cruise);
            }
        }
        return handled;
    }

    private static boolean putLight(List<Esp32NavState.Light> target, int dir, int status,
                                    int seconds, boolean cruise) {
        if (dir < AMapConstants.DIR_UTURN || dir > AMapConstants.DIR_DIAGONAL_RIGHT_2
                || status < AMapConstants.LIGHT_STATUS_YELLOW_0
                || status > AMapConstants.LIGHT_STATUS_YELLOW_6 || seconds <= 0) {
            return false;
        }
        Esp32NavState.Light light = new Esp32NavState.Light();
        light.dir = dir;
        light.status = status;
        light.seconds = Math.min(seconds, 999);
        light.updatedAt = System.currentTimeMillis();
        light.ttlMs = cruise ? seconds * 1000L + 2000L : 4500L;
        replaceByDir(target, light);
        return true;
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
        return red > 0 ? red : green;
    }

    private static int displayStatus(int status, int red, int green, int seconds) {
        if (status == AMapConstants.LIGHT_STATUS_GREEN && red == 0 && green == 0) {
            green = seconds;
        }
        if ((green == 3 && red == 0) || (green > 0 && green < 3)) {
            return AMapConstants.LIGHT_STATUS_YELLOW_2;
        }
        if (status < 0 && seconds > 0) {
            return green > 0 ? AMapConstants.LIGHT_STATUS_GREEN : AMapConstants.LIGHT_STATUS_RED;
        }
        return status;
    }

    static int normalizeCruiseStatus(int status) {
        if (status == 0) {
            return AMapConstants.LIGHT_STATUS_RED;
        }
        if (status == 1 || status == 2 || status == 4) {
            return AMapConstants.LIGHT_STATUS_GREEN;
        }
        // Some AMap builds already emit the standard 0..6 display status in
        // CameraLightInfoWrapper. Preserve extended/yellow states just like
        // AMap Companion instead of dropping the complete cruise payload.
        return status >= AMapConstants.LIGHT_STATUS_YELLOW_0
                && status <= AMapConstants.LIGHT_STATUS_YELLOW_6 ? status : -1;
    }

    static int normalizeCruiseDirection(int dir) {
        switch (dir) {
            case 0:
                return AMapConstants.DIR_UTURN;
            case 1:
                return AMapConstants.DIR_LEFT;
            case 2:
                return AMapConstants.DIR_STRAIGHT;
            case 3:
                return AMapConstants.DIR_RIGHT;
            default:
                // Wrapper builds differ: older ones use compact 0..3 while
                // newer ones can already use standard AMap directions 4..8.
                return dir >= AMapConstants.DIR_STRAIGHT
                        && dir <= AMapConstants.DIR_DIAGONAL_RIGHT_2 ? dir : -1;
        }
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

    private static Object value(Extras extras, String key) {
        try {
            return extras.get(key);
        } catch (Throwable ignored) {
            return null;
        }
    }

    private static boolean hasAny(Extras extras, String... keys) {
        for (String key : keys) {
            if (extras.containsKey(key)) {
                return true;
            }
        }
        return false;
    }

    private static int intValue(Extras extras, int fallback, String... keys) {
        for (String key : keys) {
            Object raw = value(extras, key);
            if (raw instanceof Number) {
                return ((Number) raw).intValue();
            }
            if (raw != null) {
                int parsed = BundleReaders.parseInt(String.valueOf(raw), Integer.MIN_VALUE);
                if (parsed != Integer.MIN_VALUE) {
                    return parsed;
                }
            }
        }
        return fallback;
    }

    private static boolean booleanValue(Extras extras, boolean fallback, String... keys) {
        for (String key : keys) {
            Object raw = value(extras, key);
            if (raw == null) {
                continue;
            }
            if (raw instanceof Boolean) {
                return (Boolean) raw;
            }
            String text = String.valueOf(raw).trim();
            if ("1".equals(text) || "true".equalsIgnoreCase(text) || "是".equals(text)) {
                return true;
            }
            if ("0".equals(text) || "false".equalsIgnoreCase(text) || "否".equals(text)) {
                return false;
            }
        }
        return fallback;
    }

    private static int[] intArrayValue(Extras extras, String... keys) {
        for (String key : keys) {
            int[] parsed = parseIntArray(value(extras, key));
            if (parsed != null && parsed.length > 0) {
                return parsed;
            }
        }
        return null;
    }

    private static int[] parseIntArray(Object raw) {
        if (raw == null) {
            return null;
        }
        if (raw instanceof int[]) {
            return (int[]) raw;
        }
        if (raw instanceof Number) {
            return new int[]{((Number) raw).intValue()};
        }
        if (raw.getClass().isArray()) {
            int length = Array.getLength(raw);
            int[] out = new int[length];
            for (int i = 0; i < length; i++) {
                Object item = Array.get(raw, i);
                out[i] = item instanceof Number ? ((Number) item).intValue()
                        : BundleReaders.parseInt(String.valueOf(item), Integer.MIN_VALUE);
            }
            return out;
        }
        String text = String.valueOf(raw).replace('[', ' ').replace(']', ' ').trim();
        if (text.isEmpty()) {
            return null;
        }
        String[] parts = text.split("[,;| ]+");
        int[] out = new int[parts.length];
        int count = 0;
        for (String part : parts) {
            int parsed = BundleReaders.parseInt(part, Integer.MIN_VALUE);
            if (parsed != Integer.MIN_VALUE) {
                out[count++] = parsed;
            }
        }
        if (count == 0) {
            return null;
        }
        int[] compact = new int[count];
        System.arraycopy(out, 0, compact, 0, count);
        return compact;
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

    private interface Extras {
        boolean containsKey(String key);
        Set<String> keySet();
        Object get(String key);
    }

    private static final class BundleExtras implements Extras {
        private final Bundle bundle;

        BundleExtras(Bundle bundle) {
            this.bundle = bundle;
        }

        @Override
        public boolean containsKey(String key) {
            return bundle.containsKey(key);
        }

        @Override
        public Set<String> keySet() {
            return bundle.keySet();
        }

        @Override
        public Object get(String key) {
            return BundleReaders.safeExtra(bundle, key);
        }
    }

    private static final class MapExtras implements Extras {
        private final Map<String, ?> values;

        MapExtras(Map<String, ?> values) {
            this.values = values;
        }

        @Override
        public boolean containsKey(String key) {
            return values.containsKey(key);
        }

        @Override
        public Set<String> keySet() {
            return values.keySet();
        }

        @Override
        public Object get(String key) {
            return values.get(key);
        }
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
