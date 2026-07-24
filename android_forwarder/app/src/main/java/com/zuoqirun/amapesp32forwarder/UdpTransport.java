package com.zuoqirun.amapesp32forwarder;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;

final class UdpTransport implements Esp32Transport {
    private final String host;
    private final int port;
    private final MediaControlListener mediaControlListener;
    private DatagramSocket socket;
    private InetAddress address;
    private Thread receiveThread;

    UdpTransport(String host, int port, MediaControlListener mediaControlListener) {
        this.host = host;
        this.port = port;
        this.mediaControlListener = mediaControlListener;
    }

    @Override
    public synchronized void start() throws IOException {
        if (socket != null && !socket.isClosed()) {
            return;
        }
        address = InetAddress.getByName(host);
        socket = new DatagramSocket();
        socket.connect(address, port);
        receiveThread = new Thread(this::receiveLoop, "esp32-media-control-udp");
        receiveThread.setDaemon(true);
        receiveThread.start();
    }

    @Override
    public synchronized void stop() {
        if (socket != null) {
            socket.close();
            socket = null;
        }
        receiveThread = null;
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
        DatagramPacket packet = new DatagramPacket(payload, payload.length);
        socket.send(packet);
    }

    private void receiveLoop() {
        byte[] buffer = new byte[256];
        while (true) {
            DatagramSocket target;
            synchronized (this) {
                target = socket;
            }
            if (target == null || target.isClosed()) {
                return;
            }
            DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
            try {
                target.receive(packet);
                byte[] payload = new byte[packet.getLength()];
                System.arraycopy(packet.getData(), packet.getOffset(), payload, 0, packet.getLength());
                String action = MediaControlCommand.parse(payload);
                if (action != null && mediaControlListener != null) {
                    mediaControlListener.onMediaControl(action);
                }
            } catch (IOException error) {
                if (!target.isClosed()) {
                    // The next heartbeat recreates the transport after a real socket failure.
                    stop();
                }
                return;
            }
        }
    }
}
