package com.zuoqirun.amapesp32forwarder;

import android.os.Bundle;

import org.json.JSONArray;
import org.json.JSONObject;

import java.lang.reflect.Array;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** Parses the single, array and JSON service-area payloads used by AMap Auto builds. */
final class ServiceAreaParser {
    private static final String[] PAYLOAD_KEYS = {
            "SAPA_LIST", "SAPA_INFO", "SAPA_INFOS", "sapaList", "sapaInfo",
            "serviceAreas", "serviceAreaInfos", "serviceAreaList"
    };

    private ServiceAreaParser() {}

    static Result parse(Bundle extras) {
        return extras == null ? Result.notHandled() : parse(new BundleSource(extras));
    }

    static Result parse(Map<String, ?> extras) {
        return extras == null ? Result.notHandled() : parse(new MapSource(extras));
    }

    private static Result parse(Source source) {
        if (!hasPayload(source)) {
            return Result.notHandled();
        }
        ArrayList<Entry> entries = new ArrayList<>();
        add(entries, string(source, "SAPA_NAME"), integer(source, "SAPA_DIST", -1),
                string(source, "SAPA_DIST_AUTO"));
        add(entries, string(source, "NEXT_SAPA_NAME"), integer(source, "NEXT_SAPA_DIST", -1),
                string(source, "NEXT_SAPA_DIST_AUTO"));
        for (String key : PAYLOAD_KEYS) {
            parseValue(source.get(key), entries);
        }

        Object names = first(source, "SAPA_NAMES", "SAPA_NAME_LIST", "SAPA_NAME_ARRAY",
                "sapaNames", "serviceAreaNames");
        Object distances = first(source, "SAPA_DISTS", "SAPA_DIST_LIST", "SAPA_DIST_ARRAY",
                "sapaDistances", "serviceAreaDistances");
        Object distanceTexts = first(source, "SAPA_DIST_AUTOS", "SAPA_DIST_AUTO_LIST",
                "sapaDistanceTexts", "serviceAreaDistanceTexts");
        int count = Math.max(length(names), Math.max(length(distances), length(distanceTexts)));
        for (int i = 0; i < count; i++) {
            add(entries, textAt(names, i), intAt(distances, i, -1), textAt(distanceTexts, i));
        }
        return new Result(true, entries);
    }

    private static boolean hasPayload(Source source) {
        String[] keys = {"SAPA_DIST", "SAPA_DIST_AUTO", "SAPA_NAME", "SAPA_TYPE", "SAPA_NUM",
                "NEXT_SAPA_DIST", "NEXT_SAPA_DIST_AUTO", "NEXT_SAPA_NAME", "NEXT_SAPA_TYPE",
                "SAPA_NAMES", "SAPA_NAME_LIST", "SAPA_NAME_ARRAY", "sapaNames",
                "SAPA_DISTS", "SAPA_DIST_LIST", "SAPA_DIST_ARRAY", "sapaDistances",
                "SAPA_DIST_AUTOS", "SAPA_DIST_AUTO_LIST", "sapaDistanceTexts",
                "serviceAreaNames", "serviceAreaDistances", "serviceAreaDistanceTexts"};
        for (String key : keys) if (source.has(key)) return true;
        for (String key : PAYLOAD_KEYS) if (source.has(key)) return true;
        return false;
    }

    private static void parseValue(Object value, List<Entry> target) {
        if (value == null) return;
        if (value instanceof Bundle) {
            Bundle bundle = (Bundle) value;
            add(target, BundleReaders.valueString(bundle, "name", "sapaName", "SAPA_NAME", "serviceAreaName"),
                    BundleReaders.intValue(bundle, -1, "dist", "distance", "SAPA_DIST"),
                    BundleReaders.valueString(bundle, "distAuto", "distanceText", "SAPA_DIST_AUTO"));
            return;
        }
        if (value instanceof Map) {
            parseObject(new JSONObject((Map<?, ?>) value), target);
            return;
        }
        if (value instanceof Iterable) {
            for (Object item : (Iterable<?>) value) parseValue(item, target);
            return;
        }
        if (value.getClass().isArray()) {
            for (int i = 0; i < Array.getLength(value); i++) parseValue(Array.get(value, i), target);
            return;
        }
        String text = String.valueOf(value).trim();
        try {
            if (text.startsWith("[")) {
                JSONArray array = new JSONArray(text);
                for (int i = 0; i < array.length(); i++) parseObject(array.optJSONObject(i), target);
            } else if (text.startsWith("{")) {
                parseObject(new JSONObject(text), target);
            }
        } catch (Throwable ignored) {}
    }

    private static void parseObject(JSONObject object, List<Entry> target) {
        if (object == null) return;
        for (String key : new String[]{"serviceAreas", "serviceAreaInfos", "serviceAreaList", "items", "data"}) {
            JSONArray array = object.optJSONArray(key);
            if (array != null) for (int i = 0; i < array.length(); i++) parseObject(array.optJSONObject(i), target);
        }
        add(target, jsonString(object, "name", "sapaName", "SAPA_NAME", "serviceAreaName"),
                jsonInt(object, -1, "dist", "distance", "SAPA_DIST", "sapaDist"),
                jsonString(object, "distAuto", "distanceText", "SAPA_DIST_AUTO", "sapaDistAuto"));
    }

    private static void add(List<Entry> target, String name, int meters, String distanceText) {
        if (isEmpty(name) && isEmpty(distanceText) && meters < 0) return;
        if (isEmpty(distanceText) && meters >= 0) distanceText = formatDistance(meters);
        Entry candidate = new Entry(empty(name), empty(distanceText));
        for (Entry old : target) if (old.name.equals(candidate.name) && old.distance.equals(candidate.distance)) return;
        target.add(candidate);
    }

    private static String formatDistance(int meters) {
        if (meters < 1000) return meters + "米";
        float km = meters / 1000f;
        return km >= 10 ? Math.round(km) + "公里" : String.format(java.util.Locale.US, "%.1f公里", km);
    }

    private static Object first(Source s, String... keys) { for (String k : keys) if (s.has(k)) return s.get(k); return null; }
    private static String string(Source s, String key) { Object v = s.get(key); return v == null ? null : String.valueOf(v); }
    private static int integer(Source s, String key, int fallback) { Object v = s.get(key); if (v instanceof Number) return ((Number)v).intValue(); try { return v == null ? fallback : Integer.parseInt(String.valueOf(v)); } catch (Throwable e) { return fallback; } }
    private static int length(Object v) { if (v == null) return 0; if (v.getClass().isArray()) return Array.getLength(v); if (v instanceof List) return ((List<?>)v).size(); return 1; }
    private static Object at(Object v, int i) { if (v == null) return null; if (v.getClass().isArray()) return i < Array.getLength(v) ? Array.get(v, i) : null; if (v instanceof List) return i < ((List<?>)v).size() ? ((List<?>)v).get(i) : null; return i == 0 ? v : null; }
    private static String textAt(Object v, int i) { Object item = at(v, i); return item == null ? null : String.valueOf(item); }
    private static int intAt(Object v, int i, int fallback) { Object item = at(v, i); if (item instanceof Number) return ((Number)item).intValue(); try { return item == null ? fallback : Integer.parseInt(String.valueOf(item)); } catch (Throwable e) { return fallback; } }
    private static String jsonString(JSONObject o, String... keys) { for (String k : keys) { String v = o.optString(k, null); if (!isEmpty(v) && !"null".equals(v)) return v; } return null; }
    private static int jsonInt(JSONObject o, int fallback, String... keys) { for (String k : keys) if (o.has(k)) return o.optInt(k, fallback); return fallback; }
    private static String empty(String v) { return v == null ? "" : v; }
    private static boolean isEmpty(String v) { return v == null || v.isEmpty() || "null".equals(v) || "0".equals(v); }

    static final class Entry {
        final String name;
        final String distance;
        Entry(String name, String distance) { this.name = name; this.distance = distance; }
    }
    static final class Result {
        final boolean handled;
        final List<Entry> entries;
        Result(boolean handled, List<Entry> entries) { this.handled = handled; this.entries = entries; }
        static Result notHandled() { return new Result(false, new ArrayList<>()); }
    }
    private interface Source { boolean has(String key); Object get(String key); }
    private static final class BundleSource implements Source {
        final Bundle b; BundleSource(Bundle b) { this.b = b; }
        public boolean has(String k) { return b.containsKey(k); }
        public Object get(String k) { return BundleReaders.safeExtra(b, k); }
    }
    private static final class MapSource implements Source {
        final Map<String, ?> m; MapSource(Map<String, ?> m) { this.m = m; }
        public boolean has(String k) { return m.containsKey(k); }
        public Object get(String k) { return m.get(k); }
    }
}
