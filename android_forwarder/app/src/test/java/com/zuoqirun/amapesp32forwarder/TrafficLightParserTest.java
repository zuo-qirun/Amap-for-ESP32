package com.zuoqirun.amapesp32forwarder;

import org.json.JSONObject;
import org.junit.Test;

import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class TrafficLightParserTest {
    @Test
    public void lightsDataWinsOverFirstLightCompatibilityFields() {
        Map<String, Object> extras = cruisePayload(
                "[{\"status\":0,\"countdown\":18,\"dir\":1},"
                        + "{\"status\":1,\"countdown\":25,\"dir\":2}]");
        extras.put("trafficLightStatus", 0);
        extras.put("redLightCountDownSeconds", 18);
        extras.put("dir", 1);
        extras.put("lightsCount", 2);
        extras.put("clearLights", false);

        TrafficLightParser.Result result = TrafficLightParser.parse(extras, false, -1, 0);

        assertTrue(result.handled);
        assertFalse(result.clear);
        assertTrue(result.setCruiseMode);
        assertEquals(2, result.lights.size());
        assertLight(result, 0, 1, 1, 18);
        assertLight(result, 1, 4, 4, 25);
    }

    @Test
    public void cruiseGreenStatusIsNormalizedOnlyOnce() {
        TrafficLightParser.Result result = TrafficLightParser.parse(
                cruisePayload("[{\"status\":1,\"countdown\":20,\"dir\":1}]"),
                false, -1, 0);

        assertEquals(1, result.lights.size());
        assertLight(result, 0, 1, 4, 20);
    }

    @Test
    public void cruiseDirectionMappingMatchesWrapperProtocol() {
        assertEquals(0, TrafficLightParser.normalizeCruiseDirection(0));
        assertEquals(1, TrafficLightParser.normalizeCruiseDirection(1));
        assertEquals(4, TrafficLightParser.normalizeCruiseDirection(2));
        assertEquals(2, TrafficLightParser.normalizeCruiseDirection(3));
        assertEquals(4, TrafficLightParser.normalizeCruiseDirection(4));
        assertEquals(8, TrafficLightParser.normalizeCruiseDirection(8));
    }

    @Test
    public void cruiseWrapperAcceptsStandardDirectionAndExtendedStatus() {
        TrafficLightParser.Result result = TrafficLightParser.parse(
                cruisePayload("[{\"status\":3,\"countdown\":7,\"dir\":4}]"),
                false, -1, 0);

        assertTrue(result.handled);
        assertTrue(result.setCruiseMode);
        assertLight(result, 0, 4, 3, 7);
    }

    @Test
    public void explicitClearProducesEmptyUdpLightsArray() throws Exception {
        Map<String, Object> extras = cruisePayload("[ ]");
        extras.put("lightsCount", 0);
        extras.put("clearLights", true);
        extras.put("EXTRA_CLEAR_LIGHTS", true);

        TrafficLightParser.Result result = TrafficLightParser.parse(extras, true, -1, 0);
        assertTrue(result.handled);
        assertTrue(result.clear);

        Esp32NavState state = new Esp32NavState();
        Esp32NavState.Light stale = new Esp32NavState.Light();
        stale.dir = 1;
        stale.status = 1;
        stale.seconds = 18;
        state.lights.add(stale);
        if (result.clear) {
            state.lights.clear();
        }
        JSONObject udp = new JSONObject(Esp32Protocol.toJson(state, 7));
        assertEquals(0, udp.getJSONArray("lights").length());
    }

    @Test
    public void standardNavigationFieldsKeepStandardStatusAndDirection() {
        Map<String, Object> extras = new HashMap<>();
        extras.put("trafficLightStatus", 1);
        extras.put("redLightCountDownSeconds", 12);
        extras.put("dir", 4);

        TrafficLightParser.Result result = TrafficLightParser.parse(extras, false, 4, 9);

        assertTrue(result.handled);
        assertFalse(result.setCruiseMode);
        assertLight(result, 0, 4, 1, 12);
    }

    @Test
    public void invalidPayloadsDoNotCreateLightsOrEnterCruiseMode() {
        Map<String, Object> noDirection = cruisePayload(
                "[{\"status\":0,\"countdown\":18}]");
        TrafficLightParser.Result missingDir = TrafficLightParser.parse(noDirection, false, -1, 0);
        assertFalse(missingDir.handled);
        assertFalse(missingDir.setCruiseMode);

        Map<String, Object> zeroCountdown = cruisePayload(
                "[{\"status\":0,\"countdown\":0,\"dir\":1}]");
        TrafficLightParser.Result zero = TrafficLightParser.parse(zeroCountdown, false, -1, 0);
        assertFalse(zero.handled);
        assertFalse(zero.setCruiseMode);

        TrafficLightParser.Result broken = TrafficLightParser.parse(
                cruisePayload("[{broken-json"), false, -1, 0);
        assertFalse(broken.handled);
        assertFalse(broken.setCruiseMode);
    }

    @Test
    public void iterableAndJavaArrayInputsDeduplicateByNormalizedDirection() {
        Map<String, Object> first = light(2, 0, 18);
        Map<String, Object> newer = light(2, 0, 17);
        Map<String, Object> left = light(1, 1, 25);

        Map<String, Object> iterablePayload = new HashMap<>();
        iterablePayload.put("KEY_TYPE", AMapConstants.KEY_TYPE_TRAFFIC_LIGHT);
        iterablePayload.put("lightsData", Arrays.asList(first, newer, left));
        TrafficLightParser.Result iterable = TrafficLightParser.parse(iterablePayload, false, -1, 0);
        assertEquals(2, iterable.lights.size());
        assertLight(iterable, 0, 4, 1, 17);
        assertLight(iterable, 1, 1, 4, 25);

        Map<String, Object> arrayPayload = new HashMap<>();
        arrayPayload.put("KEY_TYPE", AMapConstants.KEY_TYPE_TRAFFIC_LIGHT);
        arrayPayload.put("lightsData", new Object[]{first, left});
        TrafficLightParser.Result array = TrafficLightParser.parse(arrayPayload, false, -1, 0);
        assertEquals(2, array.lights.size());
    }

    @Test
    public void nestedLightsDataObjectIsSupported() {
        Map<String, Object> extras = cruisePayload(
                "{\"lightsData\":[{\"status\":0,\"countdown\":11,\"dir\":3}]}" );
        TrafficLightParser.Result result = TrafficLightParser.parse(extras, false, -1, 0);
        assertLight(result, 0, 2, 1, 11);
    }

    @Test
    public void cameraLightInfoWrapperEntersCruiseParser() {
        Map<String, Object> extras = new HashMap<>();
        extras.put("cameraLightInfoWrapper",
                "CameraLightInfo{direction=2,status=1,countDown=9}");

        TrafficLightParser.Result result = TrafficLightParser.parse(extras, true, -1, 0);

        assertTrue(result.handled);
        assertFalse(result.clear);
        assertTrue(result.setCruiseMode);
        assertLight(result, 0, 4, 4, 9);
    }

    private static Map<String, Object> cruisePayload(String lightsData) {
        Map<String, Object> extras = new HashMap<>();
        extras.put("KEY_TYPE", AMapConstants.KEY_TYPE_TRAFFIC_LIGHT);
        extras.put("lightsData", lightsData);
        return extras;
    }

    private static Map<String, Object> light(int dir, int status, int countdown) {
        Map<String, Object> light = new HashMap<>();
        light.put("dir", dir);
        light.put("status", status);
        light.put("countdown", countdown);
        return light;
    }

    private static void assertLight(TrafficLightParser.Result result, int index,
                                    int dir, int status, int seconds) {
        Esp32NavState.Light light = result.lights.get(index);
        assertEquals(dir, light.dir);
        assertEquals(status, light.status);
        assertEquals(seconds, light.seconds);
    }
}
