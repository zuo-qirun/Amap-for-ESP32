package com.zuoqirun.amapesp32forwarder;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;

final class UdpTransport implements Esp32Transport {
    private final String host;
    private final int port;
    private DatagramSocket socket;
    private InetAddress address;

    UdpTransport(String host, int port) {
        this.host = host;
        this.port = port;
    }

    @Override
    public synchronized void start() throws IOException {
        if (socket != null && !socket.isClosed()) {
            return;
        }
        address = InetAddress.getByName(host);
        socket = new DatagramSocket();
    }

    @Override
    public synchronized void stop() {
        if (socket != null) {
            socket.close();
            socket = null;
        }
    }

    @Override
    public synchronized void send(byte[] payload) throws IOException {
        IOException firstError;
        try {
            sendOnce(payload);
            return;
        } catch (IOException error) {
            firstError = error;
            stop();
        }

        try {
            sendOnce(payload);
        } catch (IOException retryError) {
            stop();
            retryError.addSuppressed(firstError);
            throw retryError;
        }
    }

    private void sendOnce(byte[] payload) throws IOException {
        start();
        DatagramPacket packet = new DatagramPacket(payload, payload.length, address, port);
        socket.send(packet);
    }
}
