package com.zuoqirun.amapesp32forwarder;

import java.io.IOException;

interface Esp32Transport {
    void start() throws IOException;
    void stop();
    void send(byte[] payload) throws IOException;
}
