package com.zuoqirun.amapesp32forwarder;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
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
import android.widget.AdapterView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public final class MainActivity extends Activity {
    private static final int REQUEST_NOTIFICATIONS = 100;
    private static final int REQUEST_BLE_PERMISSIONS = 101;
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
    private Spinner targetAppSpinner;
    private EditText targetPackageInput;
    private List<TargetAppChoice> targetAppChoices;
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
        root.addView(text("监听高德地图车机版广播，并通过 UDP 或 BLE 发送完整导航快照到 ESP32-S3。", 14, false));

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
                new String[]{"UDP", "BLE"});
        transportSpinner.setAdapter(adapter);
        root.addView(transportSpinner);
        transportSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, android.view.View view,
                                       int position, long id) {
                if (position == 1 && !loadingSettings) {
                    requestBlePermissionsIfNeeded();
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        });

        root.addView(label("ESP32 IP"));
        ipInput = input("192.168.4.2");
        ipInput.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        root.addView(ipInput);

        root.addView(label("UDP 端口"));
        portInput = input("4210");
        portInput.setInputType(InputType.TYPE_CLASS_NUMBER);
        root.addView(portInput);

        root.addView(label("目标高德应用"));
        targetAppSpinner = new Spinner(this);
        targetAppChoices = loadTargetAppChoices();
        ArrayAdapter<TargetAppChoice> targetAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, targetAppChoices);
        targetAppSpinner.setAdapter(targetAdapter);
        root.addView(targetAppSpinner);

        targetPackageInput = input(AppSettings.DEFAULT_TARGET_PACKAGE);
        targetPackageInput.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_VARIATION_URI);
        root.addView(targetPackageInput);
        TextView targetHint = text("下拉框会列出可见的高德/地图应用；共存版未列出时可直接填写包名。主动请求会发送给该应用。", 12, false);
        targetHint.setTextColor(0xFF4B5563);
        root.addView(targetHint);
        targetAppSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, android.view.View view,
                                       int position, long id) {
                if (!loadingSettings && position >= 0 && position < targetAppChoices.size()) {
                    targetPackageInput.setText(targetAppChoices.get(position).packageName);
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        });

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

        root.addView(section("连接排查"));
        root.addView(text("1. 车机热点、手机和 ESP32 需要在同一网段。", 14, false));
        root.addView(text("2. 如果测试帧发出但 ESP32 无显示，检查热点是否开启客户端隔离。", 14, false));
        root.addView(text("3. ESP32 串口会打印本机 IP 和收到的 JSON 解析结果。", 14, false));
        root.addView(text("4. BLE 会自动扫描并连接名称以 AMap-ESP32- 开头的开发板，无需先在系统设置中配对。", 14, false));

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
        String targetPackage = AppSettings.getTargetPackage(this);
        targetPackageInput.setText(targetPackage);
        targetAppSpinner.setSelection(findTargetAppPosition(targetPackage));
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
        if (transportSpinner.getSelectedItemPosition() == 1) {
            requestBlePermissionsIfNeeded();
        }
        AppSettings.setEsp32Ip(this, ipInput.getText().toString());
        int port = 4210;
        try {
            port = Integer.parseInt(portInput.getText().toString().trim());
        } catch (Throwable ignored) {
        }
        AppSettings.setUdpPort(this, port);
        AppSettings.setTargetPackage(this, targetPackageInput.getText().toString());
    }

    private List<TargetAppChoice> loadTargetAppChoices() {
        String savedPackage = AppSettings.getTargetPackage(this);
        Map<String, TargetAppChoice> choices = new LinkedHashMap<>();
        addTargetAppChoice(choices, savedPackage, appLabel(savedPackage));
        addTargetAppChoice(choices, AppSettings.DEFAULT_TARGET_PACKAGE,
                appLabel(AppSettings.DEFAULT_TARGET_PACKAGE));

        Intent launcherIntent = new Intent(Intent.ACTION_MAIN);
        launcherIntent.addCategory(Intent.CATEGORY_LAUNCHER);
        List<TargetAppChoice> installed = new ArrayList<>();
        try {
            List<ResolveInfo> resolved = getPackageManager().queryIntentActivities(launcherIntent, 0);
            for (ResolveInfo info : resolved) {
                if (info.activityInfo == null || TextUtils.isEmpty(info.activityInfo.packageName)) {
                    continue;
                }
                String packageName = info.activityInfo.packageName;
                CharSequence loadedLabel = info.loadLabel(getPackageManager());
                String label = loadedLabel == null ? packageName : loadedLabel.toString();
                if (!packageName.equals(getPackageName()) && isAmapCandidate(label, packageName)) {
                    installed.add(new TargetAppChoice(label, packageName));
                }
            }
        } catch (Throwable ignored) {
            // The saved/default package and manual input remain available when
            // the device limits package visibility.
        }
        Collections.sort(installed, (left, right) ->
                left.displayName.compareToIgnoreCase(right.displayName));
        for (TargetAppChoice choice : installed) {
            addTargetAppChoice(choices, choice.packageName, choice.label);
        }
        return new ArrayList<>(choices.values());
    }

    private void addTargetAppChoice(Map<String, TargetAppChoice> choices,
                                    String packageName, String label) {
        String normalized = AppSettings.normalizeTargetPackage(packageName);
        if (!choices.containsKey(normalized)) {
            choices.put(normalized, new TargetAppChoice(label, normalized));
        }
    }

    private String appLabel(String packageName) {
        try {
            CharSequence label = getPackageManager().getApplicationLabel(
                    getPackageManager().getApplicationInfo(packageName, 0));
            return label == null ? packageName : label.toString();
        } catch (Throwable ignored) {
            return packageName;
        }
    }

    private boolean isAmapCandidate(String label, String packageName) {
        String searchable = (label + " " + packageName).toLowerCase(Locale.ROOT);
        if (searchable.contains("companion") || searchable.contains("forwarder")) {
            return false;
        }
        return searchable.contains("amap")
                || searchable.contains("autonavi")
                || searchable.contains("gaode")
                || searchable.contains("高德");
    }

    private int findTargetAppPosition(String packageName) {
        for (int i = 0; i < targetAppChoices.size(); i++) {
            if (targetAppChoices.get(i).packageName.equals(packageName)) {
                return i;
            }
        }
        return 0;
    }

    private static final class TargetAppChoice {
        final String label;
        final String packageName;
        final String displayName;

        TargetAppChoice(String label, String packageName) {
            this.label = TextUtils.isEmpty(label) ? packageName : label;
            this.packageName = packageName;
            this.displayName = this.label + " (" + packageName + ")";
        }

        @Override
        public String toString() {
            return displayName;
        }
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
            if (TextUtils.isEmpty(error)) {
                return "BLE 模式会自动连接 AMap-ESP32-*，不需要 Wi-Fi 或手动配对。";
            }
            String lower = error.toLowerCase(Locale.US);
            if (lower.contains("权限") || lower.contains("permission")) {
                return "请允许“附近设备”权限，然后保持手机蓝牙开启。";
            }
            if (lower.contains("未找到") || lower.contains("扫描") || lower.contains("scan")) {
                return "请确认开发板已启动 BLE，且手机靠近开发板。";
            }
            return "BLE 连接会在下一次心跳自动重试；无需在系统蓝牙页面配对。";
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
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQUEST_NOTIFICATIONS);
        }
    }

    private void requestBlePermissionsIfNeeded() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED
                    || checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{
                        Manifest.permission.BLUETOOTH_SCAN,
                        Manifest.permission.BLUETOOTH_CONNECT
                }, REQUEST_BLE_PERMISSIONS);
                return;
            }
        } else if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION},
                    REQUEST_BLE_PERMISSIONS);
            return;
        }
        BluetoothManager manager = (BluetoothManager) getSystemService(BLUETOOTH_SERVICE);
        BluetoothAdapter adapter = manager == null ? null : manager.getAdapter();
        if (adapter != null && !adapter.isEnabled()) {
            try {
                startActivity(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE));
            } catch (Throwable ignored) {
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQUEST_BLE_PERMISSIONS) {
            return;
        }
        boolean granted = grantResults.length > 0;
        for (int result : grantResults) {
            granted &= result == PackageManager.PERMISSION_GRANTED;
        }
        if (granted && AppSettings.isEnabled(this)
                && AppSettings.TRANSPORT_BLE.equals(AppSettings.getTransport(this))) {
            startForwarder(ForwarderService.ACTION_START);
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
