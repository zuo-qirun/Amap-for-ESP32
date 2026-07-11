package com.zuoqirun.amapesp32forwarder;

import org.junit.Test;

import java.util.HashMap;
import java.util.Map;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class ServiceAreaParserTest {
    @Test public void parsesCurrentAndNextSapaFields() {
        Map<String, Object> extras = new HashMap<>();
        extras.put("SAPA_NAME", "清河服务区");
        extras.put("SAPA_DIST", 8600);
        extras.put("NEXT_SAPA_NAME", "百葛服务区");
        extras.put("NEXT_SAPA_DIST_AUTO", "31公里");
        ServiceAreaParser.Result result = ServiceAreaParser.parse(extras);
        assertTrue(result.handled);
        assertEquals(2, result.entries.size());
        assertEquals("8.6公里", result.entries.get(0).distance);
        assertEquals("百葛服务区", result.entries.get(1).name);
    }

    @Test public void parsesJsonListAndEmptyListClearsState() {
        Map<String, Object> extras = new HashMap<>();
        extras.put("serviceAreas", "[{\"name\":\"A服务区\",\"distance\":900},{\"name\":\"B服务区\",\"distAuto\":\"12公里\"}]");
        ServiceAreaParser.Result result = ServiceAreaParser.parse(extras);
        assertEquals(2, result.entries.size());
        assertEquals("900米", result.entries.get(0).distance);

        extras.put("serviceAreas", "[]");
        result = ServiceAreaParser.parse(extras);
        assertTrue(result.handled);
        assertTrue(result.entries.isEmpty());
    }
}
