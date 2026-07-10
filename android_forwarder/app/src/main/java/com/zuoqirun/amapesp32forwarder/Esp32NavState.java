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
    final Tmc tmc = new Tmc();
    final Route route = new Route();
    final RoadInfo roadInfo = new RoadInfo();
    final GuideInfo guide = new GuideInfo();
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
        out.tmc.totalDistance = tmc.totalDistance;
        out.tmc.finishDistance = tmc.finishDistance;
        for (TmcSegment segment : tmc.segments) {
            out.tmc.segments.add(segment.copy());
        }
        out.route.remainingMeters = route.remainingMeters;
        out.route.totalMeters = route.totalMeters;
        out.route.remainingSeconds = route.remainingSeconds;
        out.route.progressPercent = route.progressPercent;
        out.route.destination = route.destination;
        out.route.remainingTrafficLights = route.remainingTrafficLights;
        out.roadInfo.type = roadInfo.type;
        out.roadInfo.bearing = roadInfo.bearing;
        out.roadInfo.traffic = roadInfo.traffic;
        out.roadInfo.crossMap = roadInfo.crossMap;
        out.guide.exitName = guide.exitName;
        out.guide.exitDirection = guide.exitDirection;
        out.guide.serviceAreaName = guide.serviceAreaName;
        out.guide.serviceAreaDistance = guide.serviceAreaDistance;
        out.guide.nextServiceAreaName = guide.nextServiceAreaName;
        out.guide.nextServiceAreaDistance = guide.nextServiceAreaDistance;
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
        sb.append('|').append(tmc.totalDistance).append('|').append(tmc.finishDistance);
        for (TmcSegment segment : tmc.segments) {
            sb.append('|').append(segment.status).append(',').append(segment.distance);
        }
        sb.append('|').append(route.remainingMeters).append('|').append(route.totalMeters).append('|')
                .append(route.remainingSeconds).append('|').append(route.progressPercent).append('|')
                .append(route.destination).append('|').append(route.remainingTrafficLights);
        sb.append('|').append(roadInfo.type).append('|').append(roadInfo.bearing).append('|')
                .append(roadInfo.traffic).append('|').append(roadInfo.crossMap);
        sb.append('|').append(guide.exitName).append('|').append(guide.exitDirection).append('|')
                .append(guide.serviceAreaName).append('|').append(guide.serviceAreaDistance).append('|')
                .append(guide.nextServiceAreaName).append('|').append(guide.nextServiceAreaDistance);
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
        state.tmc.totalDistance = 28600;
        state.tmc.finishDistance = 16300;
        state.tmc.segments.add(new TmcSegment(10, 4200));
        state.tmc.segments.add(new TmcSegment(1, 8700));
        state.tmc.segments.add(new TmcSegment(2, 3900));
        state.tmc.segments.add(new TmcSegment(3, 5200));
        state.tmc.segments.add(new TmcSegment(1, 6600));
        state.route.remainingMeters = 12300;
        state.route.totalMeters = 28600;
        state.route.remainingSeconds = 1080;
        state.route.progressPercent = 57;
        state.route.destination = "目的地";
        state.route.remainingTrafficLights = 4;
        state.roadInfo.type = "高速";
        state.roadInfo.bearing = 90;
        state.roadInfo.traffic = "前方畅通";
        state.guide.exitName = "学院路出口";
        state.guide.exitDirection = "靠右驶离";
        state.guide.serviceAreaName = "清河服务区";
        state.guide.serviceAreaDistance = "8.6公里";
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

    static final class Tmc {
        static final int MAX_SEGMENTS = 8;
        int totalDistance = -1;
        int finishDistance = -1;
        final List<TmcSegment> segments = new ArrayList<>();

        void clear() {
            totalDistance = -1;
            finishDistance = -1;
            segments.clear();
        }
    }

    static final class TmcSegment {
        int status;
        int distance;

        TmcSegment(int status, int distance) {
            this.status = status;
            this.distance = distance;
        }

        TmcSegment copy() {
            return new TmcSegment(status, distance);
        }
    }

    static final class Route {
        int remainingMeters = -1;
        int totalMeters = -1;
        int remainingSeconds = -1;
        int progressPercent = -1;
        String destination = "";
        int remainingTrafficLights = -1;
    }

    static final class RoadInfo {
        String type = "";
        int bearing = -1;
        String traffic = "";
        boolean crossMap;
    }

    static final class GuideInfo {
        String exitName = "";
        String exitDirection = "";
        String serviceAreaName = "";
        String serviceAreaDistance = "";
        String nextServiceAreaName = "";
        String nextServiceAreaDistance = "";
    }
}
