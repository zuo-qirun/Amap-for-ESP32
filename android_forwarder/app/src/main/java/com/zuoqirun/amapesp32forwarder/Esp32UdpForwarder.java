package com.zuoqirun.amapesp32forwarder;

import android.content.Context;
import android.text.TextUtils;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicLong;

final class Esp32UdpForwarder {
    private static final long MIN_INTERVAL_MS = 120L;

    private final Context appContext;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final AtomicLong seq = new AtomicLong(1L);
    private long lastSendAt;
    private String lastFingerprint = "";
    private Esp32Transport transport;
    private String transportKey = "";

    Esp32UdpForwarder(Context context) {
        this.appContext = context.getApplicationContext();
    }

    void send(Esp32NavState state, boolean force) {
        if (!AppSettings.isEnabled(appContext)) {
            return;
        }
        String fingerprint = state.fingerprint();
        long now = System.currentTimeMillis();
        boolean changed = !TextUtils.equals(fingerprint, lastFingerprint);
        if (!force && !changed) {
            return;
        }
        if (!force && now - lastSendAt < MIN_INTERVAL_MS) {
            return;
        }
        lastFingerprint = fingerprint;
        lastSendAt = now;
        Esp32NavState snapshot = state.copy();
        executor.execute(() -> doSend(snapshot));
    }

    void stop() {
        if (transport != null) {
            transport.stop();
            transport = null;
        }
    }

    void shutdown() {
        stop();
        executor.shutdownNow();
    }

    private void doSend(Esp32NavState snapshot) {
        try {
            Esp32Transport target = ensureTransport();
            byte[] payload = Esp32Protocol.encode(snapshot, seq.getAndIncrement());
            target.send(payload);
            AppSettings.noteSent(appContext, payload.length);
        } catch (Throwable t) {
            AppSettings.noteError(appContext, readableError(t));
        }
    }

    private Esp32Transport ensureTransport() throws Exception {
        String mode = AppSettings.getTransport(appContext);
        String key = mode + ":" + AppSettings.getEsp32Ip(appContext) + ":" + AppSettings.getUdpPort(appContext);
        if (transport != null && TextUtils.equals(key, transportKey)) {
            return transport;
        }
        stop();
        if (AppSettings.TRANSPORT_BLE.equals(mode)) {
            transport = new BleTransport();
        } else {
            transport = new UdpTransport(AppSettings.getEsp32Ip(appContext), AppSettings.getUdpPort(appContext));
        }
        transportKey = key;
        transport.start();
        return transport;
    }

    private static String readableError(Throwable t) {
        String message = t.getMessage();
        if (TextUtils.isEmpty(message)) {
            message = t.getClass().getSimpleName();
        }
        return message;
    }
}
