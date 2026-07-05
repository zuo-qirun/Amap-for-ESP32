package com.zuoqirun.amapesp32forwarder;

import android.os.Bundle;
import android.text.TextUtils;

import java.lang.reflect.Array;
import java.util.Arrays;

final class BundleReaders {
    private BundleReaders() {}

    static Object safeExtra(Bundle extras, String key) {
        try {
            return extras == null ? null : extras.get(key);
        } catch (Throwable ignored) {
            return null;
        }
    }

    static boolean hasAny(Bundle extras, String... keys) {
        if (extras == null) {
            return false;
        }
        for (String key : keys) {
            if (extras.containsKey(key)) {
                return true;
            }
        }
        return false;
    }

    static int intValue(Bundle extras, int fallback, String... keys) {
        for (String key : keys) {
            Object value = safeExtra(extras, key);
            if (value == null) {
                continue;
            }
            if (value instanceof Number) {
                return ((Number) value).intValue();
            }
            try {
                return Integer.parseInt(String.valueOf(value).trim());
            } catch (Throwable ignored) {
            }
        }
        return fallback;
    }

    static double doubleValue(Bundle extras, double fallback, String... keys) {
        for (String key : keys) {
            Object value = safeExtra(extras, key);
            if (value == null) {
                continue;
            }
            if (value instanceof Number) {
                return ((Number) value).doubleValue();
            }
            try {
                return Double.parseDouble(String.valueOf(value).trim());
            } catch (Throwable ignored) {
            }
        }
        return fallback;
    }

    static boolean booleanValue(Bundle extras, boolean fallback, String... keys) {
        for (String key : keys) {
            Object value = safeExtra(extras, key);
            if (value == null) {
                continue;
            }
            if (value instanceof Boolean) {
                return (Boolean) value;
            }
            String s = String.valueOf(value);
            if ("1".equals(s) || "true".equalsIgnoreCase(s) || "是".equals(s)) {
                return true;
            }
            if ("0".equals(s) || "false".equalsIgnoreCase(s) || "否".equals(s)) {
                return false;
            }
        }
        return fallback;
    }

    static String valueString(Bundle extras, String... keys) {
        for (String key : keys) {
            Object value = safeExtra(extras, key);
            if (value == null) {
                continue;
            }
            String s = String.valueOf(value);
            if (!TextUtils.isEmpty(s) && !"0".equals(s) && !"null".equalsIgnoreCase(s)) {
                return s;
            }
        }
        return "";
    }

    static int[] intArrayValue(Bundle extras, String... keys) {
        for (String key : keys) {
            int[] parsed = parseIntArray(safeExtra(extras, key));
            if (parsed != null && parsed.length > 0) {
                return parsed;
            }
        }
        return null;
    }

    static boolean[] booleanArrayValue(Bundle extras, String... keys) {
        for (String key : keys) {
            boolean[] parsed = parseBooleanArray(safeExtra(extras, key));
            if (parsed != null && parsed.length > 0) {
                return parsed;
            }
        }
        return null;
    }

    static int[] parseIntArray(Object value) {
        if (value == null) {
            return null;
        }
        if (value instanceof int[]) {
            return (int[]) value;
        }
        if (value instanceof Integer) {
            return new int[]{(Integer) value};
        }
        Class<?> cls = value.getClass();
        if (cls.isArray()) {
            int length = Array.getLength(value);
            int[] out = new int[length];
            for (int i = 0; i < length; i++) {
                Object item = Array.get(value, i);
                out[i] = item instanceof Number ? ((Number) item).intValue()
                        : parseInt(String.valueOf(item), 1);
            }
            return out;
        }
        String s = String.valueOf(value).replace('[', ' ').replace(']', ' ').trim();
        if (TextUtils.isEmpty(s)) {
            return null;
        }
        String[] parts = s.split("[,;| ]+");
        int[] out = new int[parts.length];
        int count = 0;
        for (String part : parts) {
            if (!TextUtils.isEmpty(part)) {
                out[count++] = parseInt(part, 1);
            }
        }
        return count == 0 ? null : Arrays.copyOf(out, count);
    }

    static boolean[] parseBooleanArray(Object value) {
        if (value == null) {
            return null;
        }
        if (value instanceof boolean[]) {
            return (boolean[]) value;
        }
        if (value instanceof Boolean) {
            return new boolean[]{(Boolean) value};
        }
        Class<?> cls = value.getClass();
        if (cls.isArray()) {
            int length = Array.getLength(value);
            boolean[] out = new boolean[length];
            for (int i = 0; i < length; i++) {
                out[i] = parseBoolean(Array.get(value, i));
            }
            return out;
        }
        String s = String.valueOf(value).replace('[', ' ').replace(']', ' ').trim();
        if (TextUtils.isEmpty(s)) {
            return null;
        }
        String[] parts = s.split("[,;| ]+");
        boolean[] out = new boolean[parts.length];
        int count = 0;
        for (String part : parts) {
            if (!TextUtils.isEmpty(part)) {
                out[count++] = parseBoolean(part);
            }
        }
        return count == 0 ? null : Arrays.copyOf(out, count);
    }

    static int parseInt(String value, int fallback) {
        try {
            return Integer.parseInt(value.trim());
        } catch (Throwable ignored) {
            return fallback;
        }
    }

    static boolean parseBoolean(Object value) {
        if (value instanceof Boolean) {
            return (Boolean) value;
        }
        String s = String.valueOf(value);
        return "1".equals(s) || "true".equalsIgnoreCase(s) || "是".equals(s);
    }
}
