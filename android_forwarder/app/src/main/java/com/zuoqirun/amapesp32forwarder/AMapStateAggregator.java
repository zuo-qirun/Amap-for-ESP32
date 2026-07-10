package com.zuoqirun.amapesp32forwarder;

import android.content.Intent;
import android.os.Bundle;
import android.text.TextUtils;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

final class AMapStateAggregator {
    private static final long ALERT_TTL_MS = 5200L;

    private final Esp32NavState state = new Esp32NavState();
    private boolean inCruiseMode;
    private int navigationTurnDir = -1;
    private int currentTurnIcon;
    private long alertUpdatedAt;

    synchronized Esp32NavState handleIntent(Intent intent) {
        if (intent == null || intent.getExtras() == null) {
            return snapshot();
        }
        Bundle extras = intent.getExtras();
        int keyType = keyType(extras);
        updateMode(extras, keyType);
        updateStatus(extras);
        updateAlert(extras);
        updateTurn(extras, keyType);
        updateEta(extras);
        updateGuideInfo(extras);
        updateTmc(extras, keyType);
        updateLane(extras);
        if (isTrafficLightAction(intent.getAction()) || TrafficLightParser.hasTrafficLightPayload(extras)) {
            updateTrafficLights(extras);
        }
        state.updatedAt = System.currentTimeMillis();
        return snapshot();
    }

    synchronized Esp32NavState snapshot() {
        long now = System.currentTimeMillis();
        Esp32NavState copy = state.copy();
        copy.lights.clear();
        for (Esp32NavState.Light light : state.lights) {
            long elapsed = Math.max(0L, (now - light.updatedAt) / 1000L);
            int remain = Math.max(0, light.seconds - (int) elapsed);
            if (remain > 0 && now - light.updatedAt <= light.ttlMs) {
                Esp32NavState.Light out = light.copy();
                out.seconds = remain;
                copy.lights.add(out);
            }
        }
        if (alertUpdatedAt > 0 && now - alertUpdatedAt > ALERT_TTL_MS) {
            copy.alert = "";
            copy.camera.type = -1;
            copy.camera.distance = -1;
            copy.camera.speedLimit = copy.speed.limit;
        }
        copy.speed.overspeedLevel = overspeed(copy.speed.current, copy.speed.limit);
        return copy;
    }

    private void updateMode(Bundle extras, int keyType) {
        int navState = BundleReaders.intValue(extras, -1, "EXTRA_STATE", "state");
        int type = BundleReaders.intValue(extras, -1, "TYPE", "type");
        int speed = BundleReaders.intValue(extras, -1, "CUR_SPEED", "SPEED", "speed");
        String road = BundleReaders.valueString(extras, "CUR_ROAD_NAME", "curRoadName",
                "NEXT_ROAD_NAME", "ROAD_NAME", "roadName");
        boolean hasRoute = BundleReaders.hasAny(extras, "ROUTE_REMAIN_DIS_AUTO", "ROUTE_REMAIN_TIME_AUTO",
                "ROUTE_REMAIN_DIS", "ROUTE_REMAIN_TIME", "ETA_TEXT", "etaText");

        if (keyType == AMapConstants.KEY_TYPE_NAVIGATION_STATE && navState == AMapConstants.NAV_STATE_CRUISE) {
            setMode("cruise", true);
        } else if (keyType == AMapConstants.KEY_TYPE_NAVIGATION_STATE && navState == AMapConstants.NAV_STATE_CRUISE_EXIT) {
            clearNavigation();
            setMode("standby", false);
        } else if (keyType == AMapConstants.KEY_TYPE_NAVIGATION_STATE && navState == AMapConstants.NAV_STATE_NAVIGATING) {
            setMode("nav", true);
        } else if (keyType == AMapConstants.KEY_TYPE_NAVIGATION_STATE && navState == AMapConstants.NAV_STATE_NAV_EXIT) {
            clearNavigation();
            setMode("standby", false);
        } else if (type == 2 || keyType == AMapConstants.KEY_TYPE_CRUISE
                || (!hasRoute && (speed >= 0 || !TextUtils.isEmpty(road)))) {
            setMode("cruise", true);
        } else if (keyType == AMapConstants.KEY_TYPE_ROUTE_GUIDANCE || hasRoute) {
            setMode("nav", true);
        }

        if (!TextUtils.isEmpty(road)) {
            state.road = road;
        }
        if (speed >= 0) {
            state.speed.current = speed;
        }
    }

    private void setMode(String mode, boolean active) {
        state.mode = mode;
        state.active = active;
        inCruiseMode = "cruise".equals(mode);
    }

    private void clearNavigation() {
        state.active = false;
        state.mode = "standby";
        state.road = "";
        state.heading = "";
        clearTurn();
        state.eta.remainDistanceText = "";
        state.eta.remainTimeText = "";
        state.eta.arriveTimeText = "";
        state.lane.lanes = null;
        state.lane.advised = null;
        state.lights.clear();
        state.alert = "";
        state.detail = "";
        state.camera.type = -1;
        state.camera.distance = -1;
        state.camera.speedLimit = -1;
        state.tmc.clear();
        state.route.remainingMeters = -1;
        state.route.totalMeters = -1;
        state.route.remainingSeconds = -1;
        state.route.progressPercent = -1;
        state.route.destination = "";
        state.route.remainingTrafficLights = -1;
        state.roadInfo.type = "";
        state.roadInfo.bearing = -1;
        state.roadInfo.traffic = "";
        state.roadInfo.crossMap = false;
        state.guide.exitName = "";
        state.guide.exitDirection = "";
        state.guide.serviceAreaName = "";
        state.guide.serviceAreaDistance = "";
        state.guide.nextServiceAreaName = "";
        state.guide.nextServiceAreaDistance = "";
        navigationTurnDir = -1;
        currentTurnIcon = 0;
        alertUpdatedAt = 0L;
    }

    private void updateTurn(Bundle extras, int keyType) {
        if (keyType != AMapConstants.KEY_TYPE_ROUTE_GUIDANCE) {
            return;
        }
        if (inCruiseMode) {
            clearTurn();
            return;
        }
        int icon = BundleReaders.intValue(extras, 0, "NEW_ICON", "ICON", "icon", "turnIcon");
        if (icon <= 0) {
            clearTurn();
            return;
        }
        currentTurnIcon = icon;
        navigationTurnDir = turnIconToTrafficDir(icon);
        state.turn.icon = icon;
        state.turn.text = turnText(icon);

        int meters = BundleReaders.intValue(extras, -1, "SEG_REMAIN_DIS", "NEXT_SEG_REMAIN_DIS",
                "segmentRemainDistance", "distanceMeters");
        String distance = BundleReaders.valueString(extras, "SEG_REMAIN_DIS_AUTO",
                "NEXT_SEG_REMAIN_DIS_AUTO", "distanceText");
        if (TextUtils.isEmpty(distance) && meters > 0) {
            distance = formatDistance(meters);
        }
        if (meters <= 0 && !TextUtils.isEmpty(distance)) {
            meters = parseDistanceMeters(distance);
        }
        state.turn.distanceMeters = meters > 0 ? meters : -1;
        state.turn.distanceText = distance;
        String nextRoad = BundleReaders.valueString(extras, "NEXT_ROAD_NAME", "nextRoadName",
                "NEXT_ROAD", "NEXT_ROAD_NAME_AUTO", "SEG_ROAD_NAME", "NEXT_SEG_ROAD_NAME",
                "ROAD_NAME", "roadName");
        state.turn.road = !TextUtils.isEmpty(nextRoad) ? nextRoad : state.road;
    }

    private void clearTurn() {
        state.turn.icon = 0;
        state.turn.text = "";
        state.turn.distanceText = "";
        state.turn.distanceMeters = -1;
        state.turn.road = "";
    }

    private void updateEta(Bundle extras) {
        String distance = BundleReaders.valueString(extras, "ROUTE_REMAIN_DIS_AUTO",
                "routeRemainDistanceAuto", "distance");
        String time = BundleReaders.valueString(extras, "ROUTE_REMAIN_TIME_AUTO",
                "routeRemainTimeAuto", "remainTime");
        String eta = BundleReaders.valueString(extras, "ETA_TEXT", "etaText", "eta",
                "arrivalTime", "arriveTime");
        int remainingMeters = BundleReaders.intValue(extras, -1, "ROUTE_REMAIN_DIS", "remainDistance");
        if (TextUtils.isEmpty(distance)) {
            int meters = remainingMeters;
            if (meters > 0) {
                distance = formatDistance(meters);
            }
        }
        if (remainingMeters >= 0) {
            state.route.remainingMeters = remainingMeters;
        }
        int totalMeters = BundleReaders.intValue(extras, -1, "ROUTE_ALL_DIS", "routeAllDistance");
        if (totalMeters > 0) {
            state.route.totalMeters = totalMeters;
            if (remainingMeters >= 0) {
                state.route.progressPercent = Math.max(0, Math.min(100,
                        Math.round((1f - (float) remainingMeters / totalMeters) * 100f)));
            }
        }
        int remainingSeconds = BundleReaders.intValue(extras, -1, "ROUTE_REMAIN_TIME", "remainTimeSeconds");
        if (TextUtils.isEmpty(time)) {
            int seconds = remainingSeconds;
            if (seconds > 0) {
                time = formatDuration(seconds);
            }
        }
        if (remainingSeconds >= 0) {
            state.route.remainingSeconds = remainingSeconds;
        }
        if (!TextUtils.isEmpty(distance)) {
            state.eta.remainDistanceText = distance;
        }
        if (!TextUtils.isEmpty(time)) {
            state.eta.remainTimeText = time;
        }
        if (!TextUtils.isEmpty(eta)) {
            state.eta.arriveTimeText = eta.replace("预计", "").replace("到达", "到").trim();
        }
        String road = BundleReaders.valueString(extras, "NEXT_ROAD_NAME", "CUR_ROAD_NAME",
                "roadName", "curRoadName");
        if (!TextUtils.isEmpty(road) && !"目的地".equals(road)) {
            state.road = road;
        }

        String destination = BundleReaders.valueString(extras, "endPOIName", "END_POI_NAME",
                "DESTINATION_NAME", "destinationName");
        if (!TextUtils.isEmpty(destination)) {
            state.route.destination = destination;
        }
        int remainingLights = BundleReaders.intValue(extras, -1, "routeRemainTrafficLightNum",
                "TRAFFIC_LIGHT_NUM", "remainingTrafficLights");
        if (remainingLights >= 0) {
            state.route.remainingTrafficLights = remainingLights;
        }
    }

    private void updateGuideInfo(Bundle extras) {
        String exitName = BundleReaders.valueString(extras, "EXIT_NAME_INFO", "exitName", "EXIT_NAME");
        String exitDirection = BundleReaders.valueString(extras, "EXIT_DIRECTION_INFO", "exitDirection",
                "EXIT_DIRECTION");
        if (!TextUtils.isEmpty(exitName)) {
            state.guide.exitName = exitName;
        }
        if (!TextUtils.isEmpty(exitDirection)) {
            state.guide.exitDirection = exitDirection;
        }

        String serviceArea = BundleReaders.valueString(extras, "SAPA_NAME", "serviceAreaName");
        String serviceAreaDistance = BundleReaders.valueString(extras, "SAPA_DIST_AUTO", "SAPA_DIST",
                "serviceAreaDistance");
        if (!TextUtils.isEmpty(serviceArea)) {
            state.guide.serviceAreaName = serviceArea;
        }
        if (!TextUtils.isEmpty(serviceAreaDistance)) {
            state.guide.serviceAreaDistance = serviceAreaDistance;
        }

        String nextServiceArea = BundleReaders.valueString(extras, "NEXT_SAPA_NAME", "nextServiceAreaName");
        String nextServiceAreaDistance = BundleReaders.valueString(extras, "NEXT_SAPA_DIST_AUTO",
                "NEXT_SAPA_DIST", "nextServiceAreaDistance");
        if (!TextUtils.isEmpty(nextServiceArea)) {
            state.guide.nextServiceAreaName = nextServiceArea;
        }
        if (!TextUtils.isEmpty(nextServiceAreaDistance)) {
            state.guide.nextServiceAreaDistance = nextServiceAreaDistance;
        }
        if (extras.containsKey("EXTRA_CROSS_MAP")) {
            state.roadInfo.crossMap = BundleReaders.intValue(extras, 0, "EXTRA_CROSS_MAP", "crossMap") != 0;
        }
    }

    private void updateTmc(Bundle extras, int keyType) {
        TmcInfoParser.update(extras, keyType, state.tmc);
    }

    private void updateLane(Bundle extras) {
        LaneInfoParser.LaneInfo laneInfo = LaneInfoParser.parse(extras);
        if (!laneInfo.handled) {
            return;
        }
        if (laneInfo.clear) {
            state.lane.lanes = null;
            state.lane.advised = null;
            return;
        }
        state.lane.lanes = laneInfo.lanes;
        state.lane.advised = laneInfo.advised;
    }

    private void updateTrafficLights(Bundle extras) {
        TrafficLightParser.Result result = TrafficLightParser.parse(extras, inCruiseMode,
                navigationTurnDir, currentTurnIcon);
        if (!result.handled) {
            return;
        }
        if (result.clear) {
            state.lights.clear();
            return;
        }
        if (result.setCruiseMode) {
            setMode("cruise", true);
        }
        state.lights.clear();
        state.lights.addAll(result.lights);
    }

    private void updateAlert(Bundle extras) {
        boolean alertPayload = BundleReaders.hasAny(extras, "LIMITED_SPEED", "CAMERA_INDEX", "CAMERA_DIST",
                "CAMERA_SPEED", "CAMERA_TYPE", "SAPA_DIST", "SAPA_NAME", "TRAFFIC_LIGHT_NUM",
                "routeRemainTrafficLightNum");
        if (!alertPayload) {
            return;
        }
        int cameraIndex = BundleReaders.intValue(extras, 0, "CAMERA_INDEX", "cameraIndex");
        int cameraDist = BundleReaders.intValue(extras, -1, "CAMERA_DIST", "cameraDistance");
        int cameraType = BundleReaders.intValue(extras, -1, "CAMERA_TYPE", "cameraType");
        int cameraSpeed = BundleReaders.intValue(extras, -1, "CAMERA_SPEED", "cameraSpeed");
        int limitedSpeed = BundleReaders.intValue(extras, -1, "LIMITED_SPEED", "limitedSpeed");
        int displaySpeed = limitedSpeed > 0 ? limitedSpeed : cameraSpeed;
        int lightNum = BundleReaders.intValue(extras, -1, "routeRemainTrafficLightNum", "TRAFFIC_LIGHT_NUM");

        ArrayList<String> parts = new ArrayList<>();
        if (displaySpeed > 0) {
            state.speed.limit = displaySpeed;
            parts.add("限速 " + displaySpeed);
        }
        if (cameraIndex != -1 && cameraDist >= 0) {
            state.camera.type = cameraType;
            state.camera.distance = cameraDist;
            state.camera.speedLimit = displaySpeed > 0 ? displaySpeed : state.speed.limit;
            parts.add(cameraTypeName(cameraType) + " " + formatDistance(cameraDist));
        }
        if (lightNum > 0) {
            parts.add("红绿灯 " + lightNum + "个");
        }
        state.alert = join(parts, "  ·  ");
        alertUpdatedAt = System.currentTimeMillis();
    }

    private void updateStatus(Bundle extras) {
        int speed = BundleReaders.intValue(extras, -1, "CUR_SPEED", "SPEED", "speed");
        if (speed >= 0) {
            state.speed.current = speed;
        }
        int roadType = BundleReaders.intValue(extras, -1, "ROAD_TYPE", "roadType");
        ArrayList<String> details = new ArrayList<>();
        if (roadType >= 0) {
            state.roadInfo.type = roadTypeName(roadType);
            details.add(state.roadInfo.type);
        }

        String locationJson = BundleReaders.valueString(extras, "EXTRA_LOCATION_INFO");
        int direction = BundleReaders.intValue(extras, -1, "CAR_DIRECTION", "DIRECTION", "bearing");
        if (direction < 0 && !TextUtils.isEmpty(locationJson)) {
            try {
                JSONObject object = new JSONObject(locationJson);
                direction = object.optInt("bearing", -1);
                if (speed < 0 && object.has("speed")) {
                    double raw = object.optDouble("speed", -1);
                    if (raw >= 0) {
                        state.speed.current = raw < 45 ? Math.round((float) raw * 3.6f) : Math.round((float) raw);
                    }
                }
            } catch (Throwable ignored) {
            }
        }
        if (direction >= 0) {
            state.heading = bearingToCompass(direction);
            state.roadInfo.bearing = direction;
        }

        String traffic = BundleReaders.valueString(extras, "EXTRA_LOCATION_TRAFFIC_INFO",
                "EXTRA_TRAFFIC_CONDITION_RESULT_MESSAGE");
        if (!TextUtils.isEmpty(traffic)) {
            state.roadInfo.traffic = traffic;
            details.add("前方路况 " + traffic);
        }
        String road = BundleReaders.valueString(extras, "CUR_ROAD_NAME", "ROAD_NAME", "roadName");
        if (!TextUtils.isEmpty(road)) {
            state.road = road;
        }
        if (!details.isEmpty()) {
            state.detail = join(details, "\n");
        }
        state.speed.overspeedLevel = overspeed(state.speed.current, state.speed.limit);
    }

    private int keyType(Bundle extras) {
        return BundleReaders.intValue(extras, -1, "KEY_TYPE", "keyType");
    }

    private static boolean isTrafficLightAction(String action) {
        return action != null && action.toLowerCase(Locale.US).contains("traffic_light");
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

    private static String turnText(int icon) {
        switch (icon) {
            case 2:
                return "左转";
            case 3:
                return "右转";
            case 4:
                return "向左前方";
            case 5:
                return "向右前方";
            case 6:
                return "向左后方";
            case 7:
                return "向右后方";
            case 8:
                return "掉头";
            case 18:
                return "靠左";
            case 19:
                return "靠右";
            case 9:
            default:
                return "直行";
        }
    }

    private static String bearingToCompass(int bearing) {
        int normalized = ((bearing % 360) + 360) % 360;
        String[] labels = {"北", "东北", "东", "东南", "南", "西南", "西", "西北"};
        int index = Math.round(normalized / 45f) % labels.length;
        return labels[index];
    }

    private static String formatDistance(int meters) {
        if (meters >= 1000) {
            return String.format(Locale.US, "%.1fkm", meters / 1000f);
        }
        return meters + "m";
    }

    private static String formatDuration(int seconds) {
        int minutes = Math.max(1, Math.round(seconds / 60f));
        return minutes + "分钟";
    }

    private static int parseDistanceMeters(String text) {
        if (TextUtils.isEmpty(text)) {
            return -1;
        }
        try {
            boolean km = text.contains("km") || text.contains("公里");
            String number = text.replace("km", "").replace("m", "")
                    .replace("公里", "").replace("米", "").trim();
            return Math.round(Float.parseFloat(number) * (km ? 1000f : 1f));
        } catch (Throwable ignored) {
            return -1;
        }
    }

    private static int overspeed(int current, int limit) {
        if (current <= 0 || limit <= 0 || current <= limit) {
            return 0;
        }
        int delta = current - limit;
        if (delta >= 20) {
            return 3;
        }
        if (delta >= 10) {
            return 2;
        }
        return 1;
    }

    private static String cameraTypeName(int type) {
        switch (type) {
            case 0:
                return "测速";
            case 1:
            case 14:
            case 23:
                return "监控";
            case 2:
            case 15:
                return "闯红灯";
            case 4:
            case 16:
                return "公交道";
            case 11:
                return "ETC测速";
            case 12:
                return "压线";
            case 21:
                return "违停";
            case 24:
                return "鸣笛";
            default:
                return "电子眼";
        }
    }

    private static String roadTypeName(int type) {
        switch (type) {
            case 0:
                return "高速";
            case 1:
                return "国道";
            case 2:
                return "省道";
            case 3:
                return "县道";
            case 6:
                return "快速路";
            default:
                return "道路类型 " + type;
        }
    }

    private static String join(List<String> values, String separator) {
        StringBuilder sb = new StringBuilder();
        for (String value : values) {
            if (TextUtils.isEmpty(value)) {
                continue;
            }
            if (sb.length() > 0) {
                sb.append(separator);
            }
            sb.append(value);
        }
        return sb.toString();
    }
}
