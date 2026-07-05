package com.zuoqirun.amapesp32forwarder;

import android.os.Bundle;
import android.text.TextUtils;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.Arrays;

final class LaneInfoParser {
    private static final String[] DRIVE_WAY_JSON_KEYS = {
            "EXTRA_DRIVE_WAY", "drive_way_info_json", "driveWayInfo"
    };

    private static final String[] LANE_ICON_KEYS = {
            "drive_way_lane_Back_icon", "trafficLaneType", "trafficLaneIcon",
            "laneBackInfo", "laneSelectInfo", "frontLane", "backLane",
            "FRONT_LANE", "BACK_LANE"
    };

    private static final String[] LANE_ADVISED_KEYS = {
            "trafficLaneAdvised", "recommend", "laneRecommend", "LANE_RECOMMEND"
    };

    private LaneInfoParser() {}

    static LaneInfo parse(Bundle extras) {
        if (extras == null) {
            return LaneInfo.notHandled();
        }

        String driveWayJson = BundleReaders.valueString(extras, DRIVE_WAY_JSON_KEYS);
        if (!TextUtils.isEmpty(driveWayJson)) {
            LaneInfo jsonLaneInfo = parseDriveWayJson(driveWayJson);
            if (jsonLaneInfo.handled) {
                return jsonLaneInfo;
            }
        }

        int keyType = BundleReaders.intValue(extras, -1, "KEY_TYPE", "keyType");
        if (keyType != AMapConstants.KEY_TYPE_LANE_INFO && !BundleReaders.hasAny(extras, LANE_ICON_KEYS)) {
            return LaneInfo.notHandled();
        }

        int[] lanes = BundleReaders.intArrayValue(extras, LANE_ICON_KEYS);
        boolean[] advised = BundleReaders.booleanArrayValue(extras, LANE_ADVISED_KEYS);
        if (lanes == null || lanes.length == 0) {
            return LaneInfo.notHandled();
        }
        return LaneInfo.data(limit(lanes), normalizeAdvised(advised, lanes.length));
    }

    private static LaneInfo parseDriveWayJson(String json) {
        if (TextUtils.isEmpty(json)) {
            return LaneInfo.notHandled();
        }
        try {
            JSONObject object = new JSONObject(json);
            if (object.has("drive_way_enabled") && !object.optBoolean("drive_way_enabled")) {
                return LaneInfo.clear();
            }
            JSONArray info = object.optJSONArray("drive_way_info");
            if (info == null) {
                info = object.optJSONArray("lanes");
            }
            if (info == null || info.length() == 0) {
                return LaneInfo.clear();
            }
            int count = Math.min(info.length(), 8);
            int[] lanes = new int[count];
            boolean[] advised = new boolean[count];
            boolean hasAdvised = false;
            for (int i = 0; i < count; i++) {
                JSONObject item = info.optJSONObject(i);
                if (item == null) {
                    lanes[i] = 1;
                    advised[i] = true;
                    continue;
                }
                lanes[i] = item.optInt("drive_way_lane_Back_icon",
                        item.optInt("trafficLaneIcon", item.optInt("trafficLaneType", 1)));
                advised[i] = item.optBoolean("trafficLaneAdvised");
                hasAdvised |= item.has("trafficLaneAdvised");
            }
            return LaneInfo.data(lanes, hasAdvised ? advised : null);
        } catch (Throwable ignored) {
            return LaneInfo.notHandled();
        }
    }

    private static int[] limit(int[] lanes) {
        return lanes.length <= 8 ? lanes : Arrays.copyOf(lanes, 8);
    }

    private static boolean[] normalizeAdvised(boolean[] advised, int laneCount) {
        if (advised == null || advised.length == 0) {
            return null;
        }
        int count = Math.min(8, laneCount);
        boolean[] out = new boolean[count];
        for (int i = 0; i < count; i++) {
            out[i] = i < advised.length && advised[i];
        }
        return out;
    }

    static final class LaneInfo {
        final boolean handled;
        final boolean clear;
        final int[] lanes;
        final boolean[] advised;

        private LaneInfo(boolean handled, boolean clear, int[] lanes, boolean[] advised) {
            this.handled = handled;
            this.clear = clear;
            this.lanes = lanes;
            this.advised = advised;
        }

        static LaneInfo notHandled() {
            return new LaneInfo(false, false, null, null);
        }

        static LaneInfo clear() {
            return new LaneInfo(true, true, null, null);
        }

        static LaneInfo data(int[] lanes, boolean[] advised) {
            return new LaneInfo(true, false, lanes, advised);
        }
    }
}
