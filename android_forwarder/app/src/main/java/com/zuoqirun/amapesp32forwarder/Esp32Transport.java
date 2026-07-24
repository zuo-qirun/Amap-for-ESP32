package com.zuoqirun.amapesp32forwarder;

import java.io.IOException;

interface Esp32Transport {
    interface MediaControlListener {
        void onMediaControl(String action);
    }

    void start() throws IOException;
    void stop();
    void send(byte[] payload) throws IOException;
}
