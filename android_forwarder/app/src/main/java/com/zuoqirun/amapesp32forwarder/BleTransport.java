package com.zuoqirun.amapesp32forwarder;

import java.io.IOException;

final class BleTransport implements Esp32Transport {
    @Override
    public void start() throws IOException {
        throw new IOException("BLE transport is reserved for a later version.");
    }

    @Override
    public void stop() {
    }

    @Override
    public void send(byte[] payload) throws IOException {
        throw new IOException("BLE transport is reserved for a later version.");
    }
}
