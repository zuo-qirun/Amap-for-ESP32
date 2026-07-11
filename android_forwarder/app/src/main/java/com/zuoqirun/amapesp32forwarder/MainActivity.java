package com.zuoqirun.amapesp32forwarder;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.provider.Settings;
import android.text.InputType;
import android.text.TextUtils;
import android.net.Uri;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public final class MainActivity extends Activity {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable statusPoller = new Runnable() {
        @Override
        public void run() {
            refreshStatus();
            handler.postDelayed(this, 1000L);
        }
    };

    private Switch enableSwitch;
    private Spinner transportSpinner;
    private EditText ipInput;
    private EditText portInput;
    private TextView lastBroadcastText;
    private TextView lastSentText;
    private TextView payloadText;
    private TextView errorText;
    private TextView networkHintText;
    private boolean loadingSettings;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestNotificationPermissionIfNeeded();
        buildUi();
        loadSettings();
    }

    @Override
    protected void onResume() {
        super.onResume();
        handler.post(statusPoller);
    }

    @Override
    protected void onPause() {
        handler.removeCallbacks(statusPoller);
        super.onPause();
    }

    private void buildUi() {
        ScrollView scrollView = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(20), dp(20), dp(28));
        scrollView.addView(root, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView title = text("AMap ESP32 Forwarder", 24, true);
        root.addView(title);
        root.addView(text("监听高德地图车机版广播，并通过 UDP 发送完整导航快照到 ESP32-S3。", 14, false));

        enableSwitch = new Switch(this);
        enableSwitch.setText("启用转发服务");
        enableSwitch.setTextSize(18);
        enableSwitch.setPadding(0, dp(18), 0, dp(8));
        root.addView(enableSwitch);
        enableSwitch.setOnCheckedChangeListener(this::onEnableChanged);

        root.addView(label("通信方式"));
        transportSpinner = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item,
                new String[]{"UDP", "BLE 预留"});
        transportSpinner.setAdapter(adapter);
        root.addView(transportSpinner);

        root.addView(label("ESP32 IP"));
        ipInput = input("192.168.4.2");
        ipInput.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        root.addView(ipInput);

        root.addView(label("UDP 端口"));
        portInput = input("4210");
        portInput.setInputType(InputType.TYPE_CLASS_NUMBER);
        root.addView(portInput);

        LinearLayout buttonRow = new LinearLayout(this);
        buttonRow.setOrientation(LinearLayout.HORIZONTAL);
        buttonRow.setGravity(Gravity.CENTER_VERTICAL);
        buttonRow.setPadding(0, dp(16), 0, dp(12));
        root.addView(buttonRow);

        Button saveButton = button("保存设置");
        saveButton.setOnClickListener(v -> {
            saveSettings();
            if (AppSettings.isEnabled(this)) {
                startForwarder(ForwarderService.ACTION_START);
            }
            refreshStatus();
        });
        buttonRow.addView(saveButton, rowButtonParams());

        Button testButton = button("发送测试帧");
        testButton.setOnClickListener(v -> {
            saveSettings();
            enableSwitch.setChecked(true);
            AppSettings.setEnabled(this, true);
            startForwarder(ForwarderService.ACTION_SEND_TEST);
            refreshStatus();
        });
        buttonRow.addView(testButton, rowButtonParams());

        root.addView(section("状态"));
        lastBroadcastText = statusLine(root, "最近广播", "尚未收到");
        lastSentText = statusLine(root, "最近发送", "尚未发送");
        payloadText = statusLine(root, "Payload", "0 bytes");
        errorText = statusLine(root, "最近错误", "无");
        networkHintText = text("", 14, false);
        networkHintText.setPadding(0, dp(12), 0, 0);
        root.addView(networkHintText);

        root.addView(section("网络排查"));
        root.addView(text("1. 车机热点、手机和 ESP32 需要在同一网段。", 14, false));
        root.addView(text("2. 如果测试帧发出但 ESP32 无显示，检查热点是否开启客户端隔离。", 14, false));
        root.addView(text("3. ESP32 串口会打印本机 IP 和收到的 JSON 解析结果。", 14, false));
        root.addView(text("4. BLE 已保留传输接口，当前版本仅实现 UDP。", 14, false));

        setContentView(scrollView);
    }

    private void onEnableChanged(CompoundButton buttonView, boolean isChecked) {
        if (loadingSettings) {
            return;
        }
        saveSettings();
        AppSettings.setEnabled(this, isChecked);
        if (isChecked) {
            requestBatteryExemption();
            startForwarder(ForwarderService.ACTION_START);
        } else {
            startForwarder(ForwarderService.ACTION_STOP);
        }
        refreshStatus();
    }

    private void loadSettings() {
        loadingSettings = true;
        boolean enabled = AppSettings.isEnabled(this);
        transportSpinner.setSelection(AppSettings.TRANSPORT_BLE.equals(AppSettings.getTransport(this)) ? 1 : 0);
        ipInput.setText(AppSettings.getEsp32Ip(this));
        portInput.setText(String.valueOf(AppSettings.getUdpPort(this)));
        enableSwitch.setChecked(enabled);
        loadingSettings = false;
        if (enabled) {
            requestBatteryExemption();
            startForwarder(ForwarderService.ACTION_START);
        }
    }

    private void requestBatteryExemption() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return;
        }
        try {
            PowerManager power = (PowerManager) getSystemService(POWER_SERVICE);
            if (power != null && !power.isIgnoringBatteryOptimizations(getPackageName())) {
                Intent intent = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                        Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            }
        } catch (Throwable ignored) {
            // Some vendor ROMs block the standard request. The service remains
            // usable, but the user must then allow background activity manually.
        }
    }

    private void saveSettings() {
        AppSettings.setTransport(this, transportSpinner.getSelectedItemPosition() == 1
                ? AppSettings.TRANSPORT_BLE : AppSettings.TRANSPORT_UDP);
        AppSettings.setEsp32Ip(this, ipInput.getText().toString());
        int port = 4210;
        try {
            port = Integer.parseInt(portInput.getText().toString().trim());
        } catch (Throwable ignored) {
        }
        AppSettings.setUdpPort(this, port);
    }

    private void startForwarder(String action) {
        Intent intent = new Intent(this, ForwarderService.class).setAction(action);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
    }

    private void refreshStatus() {
        lastBroadcastText.setText("最近广播: " + formatTime(AppSettings.getLastBroadcast(this)));
        lastSentText.setText("最近发送: " + formatTime(AppSettings.getLastSent(this)));
        payloadText.setText("Payload: " + AppSettings.getLastPayloadBytes(this) + " bytes");
        String error = AppSettings.getLastError(this);
        errorText.setText("最近错误: " + (TextUtils.isEmpty(error) ? "无" : error));
        networkHintText.setText(buildHint(error));
    }

    private String buildHint(String error) {
        if (AppSettings.TRANSPORT_BLE.equals(AppSettings.getTransport(this))) {
            return "BLE 目前只是预留选项，请切回 UDP。";
        }
        if (TextUtils.isEmpty(error)) {
            return "UDP 无连接握手；以 ESP32 OLED 或串口日志收到数据为准。";
        }
        String lower = error.toLowerCase(Locale.US);
        if (lower.contains("unknownhost") || lower.contains("failed to connect")) {
            return "请检查 ESP32 IP 是否填写正确。";
        }
        if (lower.contains("network") || lower.contains("unreachable")) {
            return "请检查手机与 ESP32 是否连接到同一热点，且热点未开启客户端隔离。";
        }
        return "请确认 UDP 端口与 ESP32 Config.h 中 AMAP_UDP_PORT 一致。";
    }

    private void requestNotificationPermissionIfNeeded() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 100);
        }
    }

    private TextView label(String value) {
        TextView textView = text(value, 13, true);
        textView.setPadding(0, dp(14), 0, dp(4));
        return textView;
    }

    private TextView section(String value) {
        TextView textView = text(value, 18, true);
        textView.setPadding(0, dp(20), 0, dp(8));
        return textView;
    }

    private TextView statusLine(LinearLayout root, String label, String value) {
        TextView textView = text(label + ": " + value, 14, false);
        textView.setPadding(0, dp(3), 0, dp(3));
        root.addView(textView);
        return textView;
    }

    private TextView text(String value, int sp, boolean bold) {
        TextView textView = new TextView(this);
        textView.setText(value);
        textView.setTextSize(sp);
        textView.setTextColor(0xFF111827);
        if (bold) {
            textView.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        }
        textView.setLineSpacing(0, 1.08f);
        return textView;
    }

    private EditText input(String hint) {
        EditText editText = new EditText(this);
        editText.setHint(hint);
        editText.setSingleLine(true);
        editText.setTextSize(16);
        return editText;
    }

    private Button button(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setAllCaps(false);
        return button;
    }

    private LinearLayout.LayoutParams rowButtonParams() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        lp.setMargins(0, 0, dp(8), 0);
        return lp;
    }

    private String formatTime(long millis) {
        if (millis <= 0L) {
            return "尚未记录";
        }
        return new SimpleDateFormat("HH:mm:ss", Locale.CHINA).format(new Date(millis));
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
