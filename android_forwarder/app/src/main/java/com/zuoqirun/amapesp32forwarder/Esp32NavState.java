package com.zuoqirun.amapesp32forwarder;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

final class Esp32NavState {
    boolean active;
    String mode = "standby";
    String road = "";
    String heading = "";
    final Turn turn = new Turn();
    final Eta eta = new Eta();
    final Speed speed = new Speed();
    final Lane lane = new Lane();
    final List<Light> lights = new ArrayList<>();
    final Camera camera = new Camera();
    String alert = "";
    String detail = "";
    long updatedAt = System.currentTimeMillis();

    Esp32NavState copy() {
        Esp32NavState out = new Esp32NavState();
        out.active = active;
        out.mode = mode;
        out.road = road;
        out.heading = heading;
        out.turn.icon = turn.icon;
        out.turn.text = turn.text;
        out.turn.distanceText = turn.distanceText;
        out.turn.distanceMeters = turn.distanceMeters;
        out.turn.road = turn.road;
        out.eta.remainDistanceText = eta.remainDistanceText;
        out.eta.remainTimeText = eta.remainTimeText;
        out.eta.arriveTimeText = eta.arriveTimeText;
        out.speed.current = speed.current;
        out.speed.limit = speed.limit;
        out.speed.overspeedLevel = speed.overspeedLevel;
        out.lane.lanes = lane.lanes == null ? null : Arrays.copyOf(lane.lanes, lane.lanes.length);
        out.lane.advised = lane.advised == null ? null : Arrays.copyOf(lane.advised, lane.advised.length);
        for (Light light : lights) {
            out.lights.add(light.copy());
        }
        out.camera.type = camera.type;
        out.camera.distance = camera.distance;
        out.camera.speedLimit = camera.speedLimit;
        out.alert = alert;
        out.detail = detail;
        out.updatedAt = updatedAt;
        return out;
    }

    String fingerprint() {
        StringBuilder sb = new StringBuilder(256);
        sb.append(active).append('|').append(mode).append('|').append(road).append('|').append(heading);
        sb.append('|').append(turn.icon).append('|').append(turn.text).append('|')
                .append(turn.distanceText).append('|').append(turn.distanceMeters).append('|').append(turn.road);
        sb.append('|').append(eta.remainDistanceText).append('|').append(eta.remainTimeText).append('|')
                .append(eta.arriveTimeText);
        sb.append('|').append(speed.current).append('|').append(speed.limit).append('|').append(speed.overspeedLevel);
        sb.append('|').append(Arrays.toString(lane.lanes)).append('|').append(Arrays.toString(lane.advised));
        for (Light light : lights) {
            sb.append('|').append(light.dir).append(',').append(light.status).append(',').append(light.seconds);
        }
        sb.append('|').append(camera.type).append('|').append(camera.distance).append('|').append(camera.speedLimit);
        sb.append('|').append(alert).append('|').append(detail);
        return sb.toString();
    }

    static Esp32NavState testFrame() {
        Esp32NavState state = new Esp32NavState();
        state.active = true;
        state.mode = "nav";
        state.road = "京藏高速";
        state.heading = "北";
        state.turn.icon = 3;
        state.turn.text = "右转";
        state.turn.distanceText = "300米";
        state.turn.distanceMeters = 300;
        state.turn.road = "学院路";
        state.eta.remainDistanceText = "12.3公里";
        state.eta.remainTimeText = "18分钟";
        state.eta.arriveTimeText = "14:35";
        state.speed.current = 63;
        state.speed.limit = 80;
        state.lane.lanes = new int[]{1, 4, 4, 2};
        state.lane.advised = new boolean[]{false, true, true, false};
        Light light = new Light();
        light.dir = 4;
        light.status = 1;
        light.seconds = 18;
        state.lights.add(light);
        state.camera.type = 1;
        state.camera.distance = 350;
        state.camera.speedLimit = 80;
        state.alert = "前方测速摄像头";
        return state;
    }

    static final class Turn {
        int icon;
        String text = "";
        String distanceText = "";
        int distanceMeters = -1;
        String road = "";
    }

    static final class Eta {
        String remainDistanceText = "";
        String remainTimeText = "";
        String arriveTimeText = "";
    }

    static final class Speed {
        int current = -1;
        int limit = -1;
        int overspeedLevel;
    }

    static final class Lane {
        int[] lanes;
        boolean[] advised;
    }

    static final class Light {
        int dir = -1;
        int status = -1;
        int seconds;
        long updatedAt = System.currentTimeMillis();
        long ttlMs = 4500L;

        Light copy() {
            Light out = new Light();
            out.dir = dir;
            out.status = status;
            out.seconds = seconds;
            out.updatedAt = updatedAt;
            out.ttlMs = ttlMs;
            return out;
        }
    }

    static final class Camera {
        int type = -1;
        int distance = -1;
        int speedLimit = -1;
    }
}
