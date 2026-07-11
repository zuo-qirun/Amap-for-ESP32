package com.zuoqirun.amapesp32forwarder;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.ParcelUuid;

import java.io.IOException;
import java.util.Collections;
import java.util.List;
import java.util.UUID;

final class BleTransport implements Esp32Transport {
    private static final UUID SERVICE_UUID = UUID.fromString(
            "6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    private static final UUID RX_UUID = UUID.fromString(
            "6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    private static final long CONNECT_TIMEOUT_MS = 18000L;
    private static final long WRITE_TIMEOUT_MS = 5000L;

    private final Context context;
    private final Object lock = new Object();
    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic rxCharacteristic;
    private IOException failure;
    private boolean active;
    private boolean connecting;
    private boolean ready;
    private boolean writeInFlight;
    private int negotiatedMtu = 23;
    private int nextFrameId = 1;

    BleTransport(Context context) {
        this.context = context.getApplicationContext();
    }

    @Override
    public void start() throws IOException {
        ensurePermissions();
        synchronized (lock) {
            if (ready && gatt != null && rxCharacteristic != null) {
                return;
            }
        }
        stop();

        BluetoothManager manager = (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
        BluetoothAdapter adapter = manager == null ? null : manager.getAdapter();
        if (adapter == null) {
            throw new IOException("此设备不支持 BLE");
        }
        if (!adapter.isEnabled()) {
            throw new IOException("蓝牙未开启");
        }
        BluetoothLeScanner targetScanner = adapter.getBluetoothLeScanner();
        if (targetScanner == null) {
            throw new IOException("无法启动 BLE 扫描");
        }

        synchronized (lock) {
            active = true;
            connecting = false;
            ready = false;
            failure = null;
            negotiatedMtu = 23;
            scanner = targetScanner;
        }
        ScanFilter filter = new ScanFilter.Builder()
                .setServiceUuid(new ParcelUuid(SERVICE_UUID))
                .build();
        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build();
        try {
            targetScanner.startScan(Collections.singletonList(filter), settings, scanCallback);
        } catch (SecurityException error) {
            stop();
            throw new IOException("缺少附近设备权限", error);
        }

        IOException startFailure = waitUntilReady(CONNECT_TIMEOUT_MS);
        stopScanQuietly();
        if (startFailure != null) {
            stop();
            throw startFailure;
        }
    }

    @Override
    public void stop() {
        BluetoothGatt targetGatt;
        synchronized (lock) {
            active = false;
            ready = false;
            connecting = false;
            writeInFlight = false;
            rxCharacteristic = null;
            failure = new IOException("BLE 连接已停止");
            targetGatt = gatt;
            gatt = null;
            lock.notifyAll();
        }
        stopScanQuietly();
        if (targetGatt != null) {
            try {
                targetGatt.disconnect();
            } catch (Throwable ignored) {
            }
            try {
                targetGatt.close();
            } catch (Throwable ignored) {
            }
        }
    }

    @Override
    public void send(byte[] payload) throws IOException {
        start();
        final int mtu;
        final int frameId;
        synchronized (lock) {
            mtu = negotiatedMtu;
            frameId = nextFrameId++ & 0xFFFF;
        }
        List<byte[]> chunks;
        try {
            chunks = BlePacketFramer.frame(payload, frameId, mtu);
        } catch (IllegalArgumentException error) {
            throw new IOException(error.getMessage(), error);
        }
        for (byte[] chunk : chunks) {
            writeChunk(chunk);
        }
    }

    private void ensurePermissions() throws IOException {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (context.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)
                    != PackageManager.PERMISSION_GRANTED
                    || context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)
                    != PackageManager.PERMISSION_GRANTED) {
                throw new IOException("请授予附近设备权限");
            }
        } else if (context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)
                != PackageManager.PERMISSION_GRANTED) {
            throw new IOException("请授予定位权限以扫描 BLE 设备");
        }
    }

    private IOException waitUntilReady(long timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        synchronized (lock) {
            while (active && !ready && failure == null) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0L) {
                    return new IOException("未找到 AMap-ESP32 BLE 设备");
                }
                try {
                    lock.wait(remaining);
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    return new IOException("BLE 连接被中断", error);
                }
            }
            if (ready) {
                return null;
            }
            return failure == null ? new IOException("BLE 连接失败") : failure;
        }
    }

    private void connect(ScanResult result) {
        BluetoothDevice device = result == null ? null : result.getDevice();
        if (device == null) {
            return;
        }
        synchronized (lock) {
            if (!active || connecting || ready) {
                return;
            }
            connecting = true;
        }
        stopScanQuietly();
        try {
            BluetoothGatt target = Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                    ? device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
                    : device.connectGatt(context, false, gattCallback);
            synchronized (lock) {
                if (!active) {
                    target.close();
                    return;
                }
                gatt = target;
            }
        } catch (Throwable error) {
            fail(new IOException("连接 AMap-ESP32 失败", error));
        }
    }

    @SuppressWarnings("deprecation")
    private void writeChunk(byte[] chunk) throws IOException {
        BluetoothGatt targetGatt;
        BluetoothGattCharacteristic targetCharacteristic;
        synchronized (lock) {
            if (!ready || gatt == null || rxCharacteristic == null) {
                throw failure == null ? new IOException("BLE 尚未连接") : failure;
            }
            targetGatt = gatt;
            targetCharacteristic = rxCharacteristic;
            writeInFlight = true;
            failure = null;
            targetCharacteristic.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
            targetCharacteristic.setValue(chunk);
        }
        boolean started;
        try {
            started = targetGatt.writeCharacteristic(targetCharacteristic);
        } catch (SecurityException error) {
            started = false;
            fail(new IOException("BLE 写入权限被拒绝", error));
        }
        if (!started) {
            synchronized (lock) {
                writeInFlight = false;
            }
            throw new IOException("无法开始 BLE 分片写入");
        }

        long deadline = System.currentTimeMillis() + WRITE_TIMEOUT_MS;
        synchronized (lock) {
            while (active && writeInFlight && failure == null) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0L) {
                    writeInFlight = false;
                    throw new IOException("BLE 分片写入超时");
                }
                try {
                    lock.wait(remaining);
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    throw new IOException("BLE 写入被中断", error);
                }
            }
            if (failure != null) {
                throw failure;
            }
            if (!active) {
                throw new IOException("BLE 连接已停止");
            }
        }
    }

    private void stopScanQuietly() {
        BluetoothLeScanner targetScanner;
        synchronized (lock) {
            targetScanner = scanner;
            scanner = null;
        }
        if (targetScanner != null) {
            try {
                targetScanner.stopScan(scanCallback);
            } catch (Throwable ignored) {
            }
        }
    }

    private void fail(IOException error) {
        synchronized (lock) {
            if (!active) {
                return;
            }
            ready = false;
            writeInFlight = false;
            failure = error;
            lock.notifyAll();
        }
    }

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            connect(result);
        }

        @Override
        public void onBatchScanResults(List<ScanResult> results) {
            if (results != null && !results.isEmpty()) {
                connect(results.get(0));
            }

        }

        @Override
        public void onScanFailed(int errorCode) {
            fail(new IOException("BLE 扫描失败，错误码 " + errorCode));
        }
    };

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt callbackGatt, int status, int newState) {
            if (status == BluetoothGatt.GATT_SUCCESS
                    && newState == android.bluetooth.BluetoothProfile.STATE_CONNECTED) {
                try {
                    if (!callbackGatt.requestMtu(517)) {
                        callbackGatt.discoverServices();
                    }
                } catch (Throwable error) {
                    fail(new IOException("BLE 服务发现启动失败", error));
                }
                return;
            }
            if (newState == android.bluetooth.BluetoothProfile.STATE_DISCONNECTED) {
                fail(new IOException("BLE 已断开，状态码 " + status));
            } else if (status != BluetoothGatt.GATT_SUCCESS) {
                fail(new IOException("BLE 连接失败，状态码 " + status));
            }
        }

        @Override
        public void onMtuChanged(BluetoothGatt callbackGatt, int mtu, int status) {
            synchronized (lock) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    negotiatedMtu = mtu;
                }
            }
            if (!callbackGatt.discoverServices()) {
                fail(new IOException("无法发现 BLE 服务"));
            }
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt callbackGatt, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                fail(new IOException("BLE 服务发现失败，状态码 " + status));
                return;
            }
            BluetoothGattService service = callbackGatt.getService(SERVICE_UUID);
            BluetoothGattCharacteristic characteristic = service == null
                    ? null : service.getCharacteristic(RX_UUID);
            if (characteristic == null) {
                fail(new IOException("设备缺少 AMap-ESP32 BLE 接收特征"));
                return;
            }
            synchronized (lock) {
                if (!active) {
                    return;
                }
                gatt = callbackGatt;
                rxCharacteristic = characteristic;
                connecting = false;
                ready = true;
                failure = null;
                lock.notifyAll();
            }
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt callbackGatt,
                                          BluetoothGattCharacteristic characteristic,
                                          int status) {
            synchronized (lock) {
                writeInFlight = false;
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    failure = new IOException("BLE 分片写入失败，状态码 " + status);
                }
                lock.notifyAll();
            }
        }
    };
}
