package com.zuoqirun.amapesp32forwarder;

import org.json.JSONArray;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;

final class Esp32Protocol {
    private Esp32Protocol() {}

    static byte[] encode(Esp32NavState state, long seq) throws Exception {
        return toJson(state, seq).getBytes(StandardCharsets.UTF_8);
    }

    static String toJson(Esp32NavState state, long seq) throws Exception {
        JSONObject root = new JSONObject();
        root.put("proto", 1);
        root.put("type", "nav_state");
        root.put("seq", seq);
        root.put("ts", System.currentTimeMillis());
        root.put("active", state.active);
        root.put("mode", safe(state.mode, 16));
        root.put("road", safe(state.road, 32));
        root.put("heading", safe(state.heading, 8));

        JSONObject turn = new JSONObject();
        turn.put("icon", state.turn.icon);
        turn.put("text", safe(state.turn.text, 16));
        turn.put("distanceText", safe(state.turn.distanceText, 16));
        turn.put("distanceMeters", state.turn.distanceMeters);
        turn.put("road", safe(state.turn.road, 32));
        root.put("turn", turn);

        JSONObject eta = new JSONObject();
        eta.put("remainDistanceText", safe(state.eta.remainDistanceText, 16));
        eta.put("remainTimeText", safe(state.eta.remainTimeText, 16));
        eta.put("arriveTimeText", safe(state.eta.arriveTimeText, 16));
        root.put("eta", eta);

        JSONObject speed = new JSONObject();
        speed.put("current", state.speed.current);
        speed.put("limit", state.speed.limit);
        speed.put("overspeedLevel", state.speed.overspeedLevel);
        root.put("speed", speed);

        JSONObject lane = new JSONObject();
        JSONArray lanes = new JSONArray();
        if (state.lane.lanes != null) {
            for (int i = 0; i < Math.min(8, state.lane.lanes.length); i++) {
                lanes.put(state.lane.lanes[i]);
            }
        }
        JSONArray advised = new JSONArray();
        if (state.lane.advised != null) {
            for (int i = 0; i < Math.min(8, state.lane.advised.length); i++) {
                advised.put(state.lane.advised[i]);
            }
        }
        lane.put("lanes", lanes);
        lane.put("advised", advised);
        root.put("lane", lane);

        JSONArray lights = new JSONArray();
        for (int i = 0; i < Math.min(4, state.lights.size()); i++) {
            Esp32NavState.Light light = state.lights.get(i);
            JSONObject item = new JSONObject();
            item.put("dir", light.dir);
            item.put("status", light.status);
            item.put("seconds", light.seconds);
            lights.put(item);
        }
        root.put("lights", lights);

        JSONObject camera = new JSONObject();
        camera.put("type", state.camera.type);
        camera.put("distance", state.camera.distance);
        camera.put("speedLimit", state.camera.speedLimit);
        root.put("camera", camera);
        root.put("alert", safe(state.alert, 48));
        root.put("detail", safe(state.detail, 96));
        return root.toString();
    }

    private static String safe(String value, int maxChars) {
        if (value == null) {
            return "";
        }
        String s = value.trim();
        if (s.length() <= maxChars) {
            return s;
        }
        return s.substring(0, maxChars);
    }
}
