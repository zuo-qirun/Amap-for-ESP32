package com.zuoqirun.amapesp32forwarder;

import org.json.JSONObject;
import org.junit.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class Esp32ProtocolPhoneTest {
    @Test
    public void phoneUpdateCarriesCompletePhoneState() throws Exception {
        Esp32NavState state = phoneState();
        JSONObject root = new JSONObject(new String(
                Esp32Protocol.encodePhoneUpdate(state, 42L), StandardCharsets.UTF_8));

        assertEquals(1, root.getInt("proto"));
        assertEquals("phone_update", root.getString("type"));
        JSONObject phone = root.getJSONObject("phone");
        assertTrue(phone.getBoolean("enabled"));
        assertEquals("call", phone.getJSONObject("notification").getString("kind"));
        assertEquals("Alice", phone.getJSONObject("notification").getString("sender"));
        assertEquals("Team stand-up", phone.getJSONObject("calendar").getString("title"));
        assertEquals(23.5, phone.getJSONObject("weather").getDouble("temperatureC"), 0.001);
        assertEquals(86, phone.getJSONObject("device").getInt("batteryPercent"));
    }

    @Test
    public void sharedNavigationFrameIncludesPhoneObject() throws Exception {
        JSONObject root = new JSONObject(Esp32Protocol.toJson(phoneState(), 43L));
        assertTrue(root.has("phone"));
        assertEquals("wifi", root.getJSONObject("phone").getJSONObject("device").getString("network"));
    }

    @Test
    public void phonePreviewIsClampedToUdpBudget() throws Exception {
        Esp32NavState state = phoneState();
        StringBuilder message = new StringBuilder();
        for (int i = 0; i < 600; i++) message.append('\u4e2d');
        state.phone.notification.body = message.toString();

        byte[] payload = Esp32Protocol.encodePhoneUpdate(state, 44L);
        JSONObject notification = new JSONObject(new String(payload, StandardCharsets.UTF_8))
                .getJSONObject("phone").getJSONObject("notification");
        assertTrue(payload.length <= 1800);
        assertTrue(notification.getString("body").codePointCount(0,
                notification.getString("body").length()) <= 96);
        assertFalse(notification.getString("body").isEmpty());
    }

    private static Esp32NavState phoneState() {
        Esp32NavState state = new Esp32NavState();
        state.phone.enabled = true;
        state.phone.notification.active = true;
        state.phone.notification.kind = "call";
        state.phone.notification.sender = "Alice";
        state.phone.notification.title = "Incoming call";
        state.phone.notification.body = "Please call me back.";
        state.phone.calendar.title = "Team stand-up";
        state.phone.calendar.location = "Room A";
        state.phone.weather.provider = "open_meteo";
        state.phone.weather.condition = "Clear";
        state.phone.weather.temperatureC = 23.5;
        state.phone.weather.aqi = 34;
        state.phone.device.batteryPercent = 86;
        state.phone.device.charging = true;
        state.phone.device.network = "wifi";
        return state;
    }
}
