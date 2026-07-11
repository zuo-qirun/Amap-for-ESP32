package com.zuoqirun.amapesp32forwarder;

import java.util.ArrayList;
import java.util.List;

final class BlePacketFramer {
    static final int HEADER_SIZE = 10;
    static final int FLAG_START = 0x01;
    static final int FLAG_END = 0x02;

    private static final int MAGIC_0 = 0x41;
    private static final int MAGIC_1 = 0x4D;
    private static final int VERSION = 1;
    private static final int MAX_ATTRIBUTE_BYTES = 512;

    private BlePacketFramer() {
    }

    static List<byte[]> frame(byte[] payload, int frameId, int mtu) {
        if (payload == null || payload.length == 0 || payload.length > 0xFFFF) {
            throw new IllegalArgumentException("BLE payload length must be between 1 and 65535 bytes");
        }
        int attributeBytes = Math.min(MAX_ATTRIBUTE_BYTES, Math.max(HEADER_SIZE + 1, mtu - 3));
        int maxChunkBytes = attributeBytes - HEADER_SIZE;
        List<byte[]> chunks = new ArrayList<>((payload.length + maxChunkBytes - 1) / maxChunkBytes);
        int offset = 0;
        while (offset < payload.length) {
            int chunkLength = Math.min(maxChunkBytes, payload.length - offset);
            byte[] chunk = new byte[HEADER_SIZE + chunkLength];
            chunk[0] = (byte) MAGIC_0;
            chunk[1] = (byte) MAGIC_1;
            chunk[2] = (byte) VERSION;
            int flags = offset == 0 ? FLAG_START : 0;
            if (offset + chunkLength == payload.length) {
                flags |= FLAG_END;
            }
            chunk[3] = (byte) flags;
            writeLe16(chunk, 4, frameId);
            writeLe16(chunk, 6, offset);
            writeLe16(chunk, 8, payload.length);
            System.arraycopy(payload, offset, chunk, HEADER_SIZE, chunkLength);
            chunks.add(chunk);
            offset += chunkLength;
        }
        return chunks;
    }

    static int readLe16(byte[] value, int offset) {
        return (value[offset] & 0xFF) | ((value[offset + 1] & 0xFF) << 8);
    }

    private static void writeLe16(byte[] value, int offset, int number) {
        value[offset] = (byte) (number & 0xFF);
        value[offset + 1] = (byte) ((number >>> 8) & 0xFF);
    }
}
