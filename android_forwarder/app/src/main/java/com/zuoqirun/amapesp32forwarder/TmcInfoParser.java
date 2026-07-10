package com.zuoqirun.amapesp32forwarder;

import android.os.Bundle;
import android.text.TextUtils;

import org.json.JSONArray;
import org.json.JSONObject;

/** Parses the standard KEY_TYPE=13011 EXTRA_TMC_SEGMENT payload into a compact
 * route-progress model suitable for a small UDP snapshot. */
final class TmcInfoParser {
    private TmcInfoParser() {}

    static boolean update(Bundle extras, int keyType, Esp32NavState.Tmc target) {
        boolean isTmc = keyType == AMapConstants.KEY_TYPE_TMC
                || BundleReaders.hasAny(extras, "EXTRA_TMC_SEGMENT", "tmcSegment", "tmc_info", "tmcInfo");
        if (!isTmc) {
            return false;
        }

        target.clear();
        String raw = BundleReaders.valueString(extras, "EXTRA_TMC_SEGMENT", "tmcSegment");
        if (TextUtils.isEmpty(raw)) {
            return true;
        }
        try {
            JSONObject root = new JSONObject(raw);
            int total = root.optInt("total_distance", root.optInt("totalDistance", 0));
            int finished = root.optInt("finish_distance", root.optInt("finishDistance", 0));
            JSONArray segments = root.optJSONArray("tmc_info");
            if (segments == null) {
                segments = root.optJSONArray("tmcInfo");
            }
            if (segments == null || segments.length() == 0) {
                return true;
            }

            int sum = 0;
            for (int i = 0; i < segments.length(); i++) {
                JSONObject item = segments.optJSONObject(i);
                if (item == null) {
                    continue;
                }
                int distance = Math.max(0, item.optInt("tmc_segment_distance",
                        item.optInt("tmcSegmentDistance", 0)));
                if (distance == 0) {
                    continue;
                }
                int status = item.optInt("tmc_status", item.optInt("tmcStatus", 0));
                sum += distance;
                if (target.segments.size() < Esp32NavState.Tmc.MAX_SEGMENTS) {
                    target.segments.add(new Esp32NavState.TmcSegment(status, distance));
                } else {
                    // Preserve the visual length without allowing a long route to
                    // consume the whole UDP frame: merge tail segments into the
                    // final visible block.
                    Esp32NavState.TmcSegment tail = target.segments.get(target.segments.size() - 1);
                    tail.distance += distance;
                    tail.status = status;
                }
            }
            if (sum <= 0) {
                return true;
            }
            target.totalDistance = total > 0 ? total : sum;
            target.finishDistance = Math.max(0, Math.min(finished, target.totalDistance));
        } catch (Throwable ignored) {
            target.clear();
        }
        return true;
    }
}
