package com.zuoqirun.amapesp32forwarder;

final class AMapConstants {
    private AMapConstants() {}

    static final String ACTION_SEND = "AUTONAVI_STANDARD_BROADCAST_SEND";
    static final String ACTION_RECV = "AUTONAVI_STANDARD_BROADCAST_RECV";

    static final String[] AMAP_ACTIONS = {
            ACTION_SEND,
            ACTION_RECV,
            "AUTO_GUIDE_INFO_FOR_INTERNAL_WIDGET",
            "AUTO_STATUS_FOR_INTERNAL_WIDGET",
            "com.autonavi.amapauto.AUTO_WIDGET_UPDATE_ROAD_NAME_INFO",
            "com.autonavi.amapauto.AUTO_WIDGET_UPDATE_SILENCE_ROADNAME_INFO",
            "com.autonavi.amapauto.AUTO_WIDGET_UPDATE_GPS_INFO",
            "com.autonavi.amapauto.AUTO_WIDGET_UPDATE_CAR_DIRECTION",
            "com.autonavi.amapauto.AUTO_WIDGET_UPDATE_CAMERA_INFO",
            "com.autonavi.amapauto.AUTO_WIDGET_UPDATE_TRAFFIC_LIGHT_INFO",
            "com.autonavi.amapauto.AUTO_WIDGET_UPDATE_CRUISE_TRAFFIC_LIGHT_INFO"
    };

    static final int KEY_TYPE_NAVIGATION_STATE = 10019;
    static final int KEY_TYPE_ROUTE_GUIDANCE = 10001;
    static final int KEY_TYPE_CRUISE = 60021;
    static final int KEY_TYPE_LANE_INFO = 13012;
    static final int KEY_TYPE_TRAFFIC_LIGHT = 60073;
    static final int KEY_TYPE_REQUEST_LANE = 10062;
    static final int KEY_TYPE_TMC = 13011;
    static final int KEY_TYPE_EXIT_INFO = 12011;

    static final int NAV_STATE_NAVIGATING = 8;
    static final int NAV_STATE_NAV_EXIT = 9;
    static final int NAV_STATE_CRUISE = 24;
    static final int NAV_STATE_CRUISE_EXIT = 25;

    static final int LIGHT_STATUS_RED = 1;
    static final int LIGHT_STATUS_GREEN = 4;

    static final int DIR_UTURN = 0;
    static final int DIR_LEFT = 1;
    static final int DIR_RIGHT = 2;
    static final int DIR_RIGHT_ALT = 3;
    static final int DIR_STRAIGHT = 4;
    static final int DIR_DIAGONAL_LEFT_1 = 5;
    static final int DIR_DIAGONAL_LEFT_2 = 6;
    static final int DIR_DIAGONAL_RIGHT_1 = 7;
    static final int DIR_DIAGONAL_RIGHT_2 = 8;
}
