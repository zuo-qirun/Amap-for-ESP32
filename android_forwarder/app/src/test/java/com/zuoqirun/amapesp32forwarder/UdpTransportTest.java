package com.zuoqirun.amapesp32forwarder;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

import org.junit.Test;

public final class UdpTransportTest {
    @Test
    public void receivesControlReplyOnSnapshotSocket() throws Exception {
        AtomicReference<String> receivedAction = new AtomicReference<>();
        CountDownLatch receivedControl = new CountDownLatch(1);
        try (DatagramSocket esp32 = new DatagramSocket(0)) {
            UdpTransport transport = new UdpTransport("127.0.0.1", esp32.getLocalPort(), action -> {
                receivedAction.set(action);
                receivedControl.countDown();
            });
            try {
                transport.start();
                transport.send("snapshot".getBytes(StandardCharsets.UTF_8));

                byte[] buffer = new byte[64];
                DatagramPacket snapshot = new DatagramPacket(buffer, buffer.length);
                esp32.receive(snapshot);
                byte[] reply = "{\"type\":\"media_control\",\"action\":\"next\"}"
                        .getBytes(StandardCharsets.UTF_8);
                esp32.send(new DatagramPacket(reply, reply.length,
                        snapshot.getAddress(), snapshot.getPort()));

                assertTrue(receivedControl.await(2, TimeUnit.SECONDS));
                assertEquals(MediaControlCommand.NEXT, receivedAction.get());
            } finally {
                transport.stop();
            }
        }
    }
}
