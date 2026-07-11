package com.zuoqirun.amapesp32forwarder;

import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.util.List;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public final class BlePacketFramerTest {
    @Test
    public void largePayloadIsFramedAndCanBeReassembled() throws Exception {
        byte[] payload = new byte[2048];
        for (int i = 0; i < payload.length; i++) {
            payload[i] = (byte) (i & 0xFF);
        }

        List<byte[]> chunks = BlePacketFramer.frame(payload, 0x1234, 517);

        assertTrue(chunks.size() > 1);
        ByteArrayOutputStream rebuilt = new ByteArrayOutputStream();
        int expectedOffset = 0;
        for (int i = 0; i < chunks.size(); i++) {
            byte[] chunk = chunks.get(i);
            assertEquals(0x41, chunk[0] & 0xFF);
            assertEquals(0x4D, chunk[1] & 0xFF);
            assertEquals(0x1234, BlePacketFramer.readLe16(chunk, 4));
            assertEquals(expectedOffset, BlePacketFramer.readLe16(chunk, 6));
            assertEquals(payload.length, BlePacketFramer.readLe16(chunk, 8));
            assertEquals(i == 0, (chunk[3] & BlePacketFramer.FLAG_START) != 0);
            assertEquals(i == chunks.size() - 1,
                    (chunk[3] & BlePacketFramer.FLAG_END) != 0);
            rebuilt.write(chunk, BlePacketFramer.HEADER_SIZE,
                    chunk.length - BlePacketFramer.HEADER_SIZE);
            expectedOffset += chunk.length - BlePacketFramer.HEADER_SIZE;
        }
        assertArrayEquals(payload, rebuilt.toByteArray());
    }

    @Test
    public void defaultMtuStillProducesValidChunks() {
        byte[] payload = new byte[32];
        List<byte[]> chunks = BlePacketFramer.frame(payload, 1, 23);
        assertEquals(4, chunks.size());
        for (byte[] chunk : chunks) {
            assertTrue(chunk.length <= 20);
        }
    }
}
