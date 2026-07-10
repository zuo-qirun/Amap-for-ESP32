package com.zuoqirun.amapesp32forwarder;

import org.json.JSONArray;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;

final class Esp32Protocol {
    private static final int MAX_UDP_SNAPSHOT_BYTES = 1800;

    private Esp32Protocol() {}

    static byte[] encode(Esp32NavState state, long seq) throws Exception {
        byte[] payload = toJson(state, seq).getBytes(StandardCharsets.UTF_8);
        if (payload.length <= MAX_UDP_SNAPSHOT_BYTES) {
            return payload;
        }

        // Keep essential navigation data intact and discard verbose,
        // lower-priority text before a Wi-Fi UDP frame can be fragmented.
        Esp32NavState compact = state.copy();
        compact.detail = "";
        compact.alert = safe(compact.alert, 24);
        compact.roadInfo.traffic = safe(compact.roadInfo.traffic, 16);
        compact.guide.nextServiceAreaName = "";
        compact.guide.nextServiceAreaDistance = "";
        compact.guide.serviceAreaName = safe(compact.guide.serviceAreaName, 16);
        compact.guide.serviceAreaDistance = safe(compact.guide.serviceAreaDistance, 12);
        trimTmc(compact.tmc, 4);
        payload = toJson(compact, seq).getBytes(StandardCharsets.UTF_8);
        return payload;
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

        JSONObject tmc = new JSONObject();
        tmc.put("totalDistance", state.tmc.totalDistance);
        tmc.put("finishDistance", state.tmc.finishDistance);
        JSONArray tmcSegments = new JSONArray();
        for (Esp32NavState.TmcSegment segment : state.tmc.segments) {
            JSONObject item = new JSONObject();
            item.put("status", segment.status);
            item.put("distance", segment.distance);
            tmcSegments.put(item);
        }
        tmc.put("segments", tmcSegments);
        root.put("tmc", tmc);

        JSONObject route = new JSONObject();
        route.put("remainingMeters", state.route.remainingMeters);
        route.put("totalMeters", state.route.totalMeters);
        route.put("remainingSeconds", state.route.remainingSeconds);
        route.put("progressPercent", state.route.progressPercent);
        route.put("destination", safe(state.route.destination, 32));
        route.put("remainingTrafficLights", state.route.remainingTrafficLights);
        root.put("route", route);

        JSONObject roadInfo = new JSONObject();
        roadInfo.put("type", safe(state.roadInfo.type, 16));
        roadInfo.put("bearing", state.roadInfo.bearing);
        roadInfo.put("traffic", safe(state.roadInfo.traffic, 32));
        roadInfo.put("crossMap", state.roadInfo.crossMap);
        root.put("roadInfo", roadInfo);

        JSONObject guide = new JSONObject();
        guide.put("exitName", safe(state.guide.exitName, 32));
        guide.put("exitDirection", safe(state.guide.exitDirection, 20));
        guide.put("serviceAreaName", safe(state.guide.serviceAreaName, 32));
        guide.put("serviceAreaDistance", safe(state.guide.serviceAreaDistance, 16));
        guide.put("nextServiceAreaName", safe(state.guide.nextServiceAreaName, 32));
        guide.put("nextServiceAreaDistance", safe(state.guide.nextServiceAreaDistance, 16));
        root.put("guide", guide);
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

    private static void trimTmc(Esp32NavState.Tmc tmc, int maxSegments) {
        while (tmc.segments.size() > maxSegments) {
            Esp32NavState.TmcSegment merged = tmc.segments.remove(tmc.segments.size() - 1);
            Esp32NavState.TmcSegment tail = tmc.segments.get(tmc.segments.size() - 1);
            tail.distance += merged.distance;
            tail.status = merged.status;
        }
    }
}
