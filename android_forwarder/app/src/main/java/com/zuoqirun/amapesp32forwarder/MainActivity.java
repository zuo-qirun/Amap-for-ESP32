package com.zuoqirun.amapesp32forwarder;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.content.res.ColorStateList;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.graphics.drawable.GradientDrawable;
import android.provider.Settings;
import android.text.InputType;
import android.text.TextUtils;
import android.net.Uri;
import android.view.Gravity;
import android.view.View;
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
import android.widget.Toast;

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
    private static final String STATE_BLE_PERMISSION_REQUEST = "ble_permission_request";
    private static final String STATE_NOTIFICATION_PERMISSION_REQUEST =
            "notification_permission_request";
    private static final String STATE_BATTERY_REQUEST_ATTEMPTED =
            "battery_request_attempted";
    private static final String STATE_PENDING_TEST_FRAME = "pending_test_frame";

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
    private EditText lyricOffsetInput;
    private TextView lastBroadcastText;
    private TextView lastSentText;
    private TextView payloadText;
    private TextView trafficDiagnosticText;
    private TextView musicStatusText;
    private TextView errorText;
    private TextView networkHintText;
    private boolean loadingSettings;
    private boolean blePermissionRequestInFlight;
    private boolean notificationPermissionRequestInFlight;
    private boolean batteryRequestAttempted;
    private boolean pendingTestFrame;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (savedInstanceState != null) {
            blePermissionRequestInFlight = savedInstanceState.getBoolean(
                    STATE_BLE_PERMISSION_REQUEST, false);
            notificationPermissionRequestInFlight = savedInstanceState.getBoolean(
                    STATE_NOTIFICATION_PERMISSION_REQUEST, false);
            batteryRequestAttempted = savedInstanceState.getBoolean(
                    STATE_BATTERY_REQUEST_ATTEMPTED, false);
            pendingTestFrame = savedInstanceState.getBoolean(STATE_PENDING_TEST_FRAME, false);
        }
        requestNotificationPermissionIfNeeded();
        buildUi();
        loadSettings();
    }

    @Override
    protected void onResume() {
        super.onResume();
        handler.post(statusPoller);
        sendPendingTestFrameIfReady();
        if (AppSettings.isEnabled(this)
                && AppSettings.TRANSPORT_BLE.equals(AppSettings.getTransport(this))
                && isBleReady()) {
            requestBatteryExemption();
        }
    }

    @Override
    protected void onPause() {
        handler.removeCallbacks(statusPoller);
        super.onPause();
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        outState.putBoolean(STATE_BLE_PERMISSION_REQUEST, blePermissionRequestInFlight);
        outState.putBoolean(STATE_NOTIFICATION_PERMISSION_REQUEST,
                notificationPermissionRequestInFlight);
        outState.putBoolean(STATE_BATTERY_REQUEST_ATTEMPTED, batteryRequestAttempted);
        outState.putBoolean(STATE_PENDING_TEST_FRAME, pendingTestFrame);
    }

    private void buildUi() {
        ScrollView scrollView = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(14), dp(14), dp(14), dp(28));
        root.setBackgroundColor(0xFFFAFBFC);
        scrollView.addView(root, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        LinearLayout hero = new LinearLayout(this);
        hero.setOrientation(LinearLayout.VERTICAL);
        hero.setPadding(dp(4), dp(10), dp(4), dp(16));
        TextView eyebrow = text("NAV  •  MUSIC  •  ESP32", 12, true);
        eyebrow.setTextColor(0xFF0F766E);
        hero.addView(eyebrow);
        TextView title = text("行途 · ESP32", 27, true);
        title.setTextColor(0xFF0B1726);
        title.setPadding(0, dp(7), 0, dp(5));
        hero.addView(title);
        TextView subtitle = text("把高德导航与网易云逐字歌词送到车载屏幕", 14, false);
        subtitle.setTextColor(0xFF64748B);
        hero.addView(subtitle);
        root.addView(hero, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        enableSwitch = new Switch(this);
        enableSwitch.setText("启用转发服务");
        enableSwitch.setTextSize(18);
        enableSwitch.setTextColor(0xFF0B1726);
        int[][] switchStates = new int[][]{
                new int[]{android.R.attr.state_checked}, new int[]{}
        };
        enableSwitch.setThumbTintList(new ColorStateList(switchStates,
                new int[]{0xFF19C3B1, 0xFF94A3B8}));
        enableSwitch.setTrackTintList(new ColorStateList(switchStates,
                new int[]{0x6619C3B1, 0x335E7184}));
        enableSwitch.setPadding(dp(16), dp(12), dp(16), dp(12));
        LinearLayout.LayoutParams switchParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        switchParams.setMargins(0, dp(12), 0, dp(6));
        root.addView(enableSwitch, switchParams);
        enableSwitch.setOnCheckedChangeListener(this::onEnableChanged);

        LinearLayout connectionPanel = flatSection(root, "01", "连接设置");
        LinearLayout navigationPanel = flatSection(root, "02", "导航设置");
        LinearLayout musicPanel = flatSection(root, "03", "网易云设置");
        LinearLayout devicePanel = flatSection(root, "04", "ESP32 设备设置");
        LinearLayout developerPanel = flatSection(root, "05", "开发者选项");

        musicStatusText = statusLine(musicPanel, "音乐读取", "正在检查通知使用权");
        Button musicAccessButton = button("打开音乐读取权限");
        musicAccessButton.setOnClickListener(v -> openMusicAccessSettings());
        musicPanel.addView(musicAccessButton);
        musicPanel.addView(label("歌词延迟校正（毫秒）"));
        lyricOffsetInput = input("0");
        lyricOffsetInput.setInputType(InputType.TYPE_CLASS_NUMBER
                | InputType.TYPE_NUMBER_FLAG_SIGNED);
        musicPanel.addView(lyricOffsetInput);
        TextView musicHint = text("正数会让歌词提前，负数会让歌词延后；建议每次以 100–200 ms 微调。需要在系统“通知使用权”中允许本应用。", 12, false);
        musicHint.setTextColor(0xFF4B5563);
        musicPanel.addView(musicHint);

        connectionPanel.addView(label("通信方式"));
        transportSpinner = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item,
                new String[]{"UDP", "BLE"});
        transportSpinner.setAdapter(adapter);
        connectionPanel.addView(transportSpinner);
        transportSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, android.view.View view,
                                       int position, long id) {
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        });

        connectionPanel.addView(label("ESP32 IP"));
        ipInput = input("192.168.4.2");
        ipInput.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        connectionPanel.addView(ipInput);

        connectionPanel.addView(label("UDP 端口"));
        portInput = input("4210");
        portInput.setInputType(InputType.TYPE_CLASS_NUMBER);
        connectionPanel.addView(portInput);

        Button deviceSettingsButton = button("打开设备控制台");
        deviceSettingsButton.setOnClickListener(v -> {
            saveSettings();
            startActivity(new Intent(this, Esp32SettingsActivity.class));
        });
        devicePanel.addView(deviceSettingsButton);
        TextView deviceHint = text("在 App 内管理 Wi‑Fi、屏幕芯片、触摸、BLE 与 OTA；需能通过当前 IP 访问 ESP32。", 12, false);
        deviceHint.setTextColor(0xFF64748B);
        deviceHint.setPadding(0, dp(6), 0, 0);
        devicePanel.addView(deviceHint);

        navigationPanel.addView(label("目标高德应用"));
        targetAppSpinner = new Spinner(this);
        targetAppChoices = loadTargetAppChoices();
        ArrayAdapter<TargetAppChoice> targetAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, targetAppChoices);
        targetAppSpinner.setAdapter(targetAdapter);
        navigationPanel.addView(targetAppSpinner);

        targetPackageInput = input(AppSettings.DEFAULT_TARGET_PACKAGE);
        targetPackageInput.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_VARIATION_URI);
        navigationPanel.addView(targetPackageInput);
        TextView targetHint = text("下拉框会列出可见的高德/地图应用；共存版未列出时可直接填写包名。主动请求会发送给该应用。", 12, false);
        targetHint.setTextColor(0xFF4B5563);
        navigationPanel.addView(targetHint);
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
            loadingSettings = true;
            enableSwitch.setChecked(true);
            loadingSettings = false;
            AppSettings.setEnabled(this, true);
            if (!AppSettings.TRANSPORT_BLE.equals(AppSettings.getTransport(this))
                    || isBleReady()) {
                requestBatteryExemption();
            }
            startForwarder(ForwarderService.ACTION_SEND_TEST);
            refreshStatus();
        });
        developerPanel.addView(testButton);

        developerPanel.addView(section("状态"));
        lastBroadcastText = statusLine(developerPanel, "最近广播", "尚未收到");
        lastSentText = statusLine(developerPanel, "最近发送", "尚未发送");
        payloadText = statusLine(developerPanel, "Payload", "0 bytes");
        trafficDiagnosticText = statusLine(developerPanel, "巡航灯诊断", "尚未收到红绿灯广播");
        errorText = statusLine(developerPanel, "最近错误", "无");
        networkHintText = text("", 14, false);
        networkHintText.setPadding(0, dp(12), 0, 0);
        developerPanel.addView(networkHintText);

        developerPanel.addView(section("连接排查"));
        developerPanel.addView(text("1. 车机热点、手机和 ESP32 需要在同一网段。", 14, false));
        developerPanel.addView(text("2. 如果测试帧发出但 ESP32 无显示，检查热点是否开启客户端隔离。", 14, false));
        developerPanel.addView(text("3. ESP32 串口会打印本机 IP 和收到的 JSON 解析结果。", 14, false));
        developerPanel.addView(text("4. BLE 会自动扫描并连接名称以 AMap-ESP32- 开头的开发板，无需先在系统设置中配对。", 14, false));

        setContentView(scrollView);
    }

    private void onEnableChanged(CompoundButton buttonView, boolean isChecked) {
        if (loadingSettings) {
            return;
        }
        saveSettings();
        AppSettings.setEnabled(this, isChecked);
        if (isChecked) {
            if (!AppSettings.TRANSPORT_BLE.equals(AppSettings.getTransport(this))
                    || isBleReady()) {
                requestBatteryExemption();
            }
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
        lyricOffsetInput.setText(String.valueOf(AppSettings.getLyricOffsetMs(this)));
        String targetPackage = AppSettings.getTargetPackage(this);
        targetPackageInput.setText(targetPackage);
        targetAppSpinner.setSelection(findTargetAppPosition(targetPackage));
        enableSwitch.setChecked(enabled);
        loadingSettings = false;
        if (enabled) {
            if (AppSettings.TRANSPORT_BLE.equals(AppSettings.getTransport(this))) {
                requestBlePermissionsIfNeeded();
            }
            if (!AppSettings.TRANSPORT_BLE.equals(AppSettings.getTransport(this))
                    || isBleReady()) {
                requestBatteryExemption();
            }
            startForwarder(ForwarderService.ACTION_START);
        }
    }

    private void requestBatteryExemption() {
        if (notificationPermissionRequestInFlight || blePermissionRequestInFlight) {
            return;
        }
        try {
            PowerManager power = (PowerManager) getSystemService(POWER_SERVICE);
            if (power != null && !power.isIgnoringBatteryOptimizations(getPackageName())) {
                Intent intent = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                        Uri.parse("package:" + getPackageName()));
                tryStartSystemActivity(intent);
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
        int lyricOffsetMs = 0;
        try {
            lyricOffsetMs = Integer.parseInt(lyricOffsetInput.getText().toString().trim());
        } catch (Throwable ignored) {
        }
        AppSettings.setLyricOffsetMs(this, lyricOffsetMs);
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
        String trafficDiagnostic = AppSettings.getLastTrafficDiagnostic(this);
        trafficDiagnosticText.setText("巡航灯诊断: "
                + (trafficDiagnostic.isEmpty() ? "尚未收到红绿灯广播" : trafficDiagnostic));
        if (musicStatusText != null) {
            musicStatusText.setText("音乐读取: " + (hasMusicListenerAccess()
                    ? MusicStateStore.describe() : "未授权，请打开通知使用权"));
        }
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
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU
                || checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                == PackageManager.PERMISSION_GRANTED) {
            return;
        }
        if (notificationPermissionRequestInFlight || blePermissionRequestInFlight) {
            return;
        }
        try {
            notificationPermissionRequestInFlight = true;
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},
                    REQUEST_NOTIFICATIONS);
        } catch (RuntimeException ignored) {
            notificationPermissionRequestInFlight = false;
        }
    }

    private void openMusicAccessSettings() {
        Intent intent = new Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS);
        if (!tryStartSystemActivity(intent)) {
            tryStartSystemActivity(new Intent(Settings.ACTION_SETTINGS));
        }
    }

    private boolean hasMusicListenerAccess() {
        String enabled = Settings.Secure.getString(getContentResolver(),
                "enabled_notification_listeners");
        if (TextUtils.isEmpty(enabled)) {
            return false;
        }
        ComponentName expected = new ComponentName(this, MusicNotificationListener.class);
        for (String item : enabled.split(":")) {
            ComponentName component = ComponentName.unflattenFromString(item);
            if (expected.equals(component)) {
                return true;
            }
        }
        return false;
    }

    private void requestBlePermissionsIfNeeded() {
        if (!hasBlePermissions()) {
            if (blePermissionRequestInFlight || notificationPermissionRequestInFlight) {
                return;
            }
            String[] permissions = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
                    ? new String[]{Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT}
                    : new String[]{Manifest.permission.ACCESS_FINE_LOCATION};
            try {
                blePermissionRequestInFlight = true;
                requestPermissions(permissions, REQUEST_BLE_PERMISSIONS);
            } catch (RuntimeException error) {
                blePermissionRequestInFlight = false;
                reportBleProblem("无法请求 BLE 权限，请在应用权限设置中允许附近设备权限");
            }
            return;
        }

        BluetoothAdapter adapter = bluetoothAdapter();
        if (adapter == null) {
            reportBleProblem("此车机不支持 BLE");
            return;
        }
        try {
            if (adapter.isEnabled()) {
                return;
            }
        } catch (RuntimeException error) {
            reportBleProblem("无法读取车机蓝牙状态，请检查附近设备权限");
            return;
        }

        BleEnableFlow.LaunchResult launchResult = BleEnableFlow.launch(
                new BleEnableFlow.Launcher() {
                    @Override
                    public boolean launchEnableRequest() {
                        return tryStartSystemActivity(
                                new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE));
                    }

                    @Override
                    public boolean launchBluetoothSettings() {
                        return tryStartSystemActivity(
                                new Intent(Settings.ACTION_BLUETOOTH_SETTINGS));
                    }
                });
        if (launchResult == BleEnableFlow.LaunchResult.SETTINGS_OPENED) {
            String message = "请在车机蓝牙设置中开启蓝牙，完成后返回本应用";
            AppSettings.noteError(this, message);
            Toast.makeText(this, message, Toast.LENGTH_LONG).show();
            return;
        }
        if (launchResult == BleEnableFlow.LaunchResult.ENABLE_REQUESTED) {
            return;
        }

        reportBleProblem("车机未提供可用的蓝牙开启页面，请从系统设置手动开启蓝牙");
    }

    private boolean hasBlePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)
                    == PackageManager.PERMISSION_GRANTED
                    && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)
                    == PackageManager.PERMISSION_GRANTED;
        }
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)
                == PackageManager.PERMISSION_GRANTED;
    }

    private boolean isBleReady() {
        if (!hasBlePermissions()) {
            return false;
        }
        BluetoothAdapter adapter = bluetoothAdapter();
        try {
            return adapter != null && adapter.isEnabled();
        } catch (RuntimeException ignored) {
            return false;
        }
    }

    private BluetoothAdapter bluetoothAdapter() {
        BluetoothManager manager = (BluetoothManager) getSystemService(BLUETOOTH_SERVICE);
        return manager == null ? null : manager.getAdapter();
    }

    private boolean tryStartSystemActivity(Intent intent) {
        try {
            if (intent.resolveActivity(getPackageManager()) == null) {
                return false;
            }
            startActivity(intent);
            return true;
        } catch (RuntimeException ignored) {
            return false;
        }
    }

    private void reportBleProblem(String message) {
        AppSettings.noteError(this, message);
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
    }

    private void sendPendingTestFrameIfReady() {
        if (!pendingTestFrame) {
            return;
        }
        if (AppSettings.TRANSPORT_BLE.equals(AppSettings.getTransport(this)) && !isBleReady()) {
            return;
        }
        pendingTestFrame = false;
        startForwarder(ForwarderService.ACTION_SEND_TEST);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_NOTIFICATIONS) {
            notificationPermissionRequestInFlight = false;
            if (AppSettings.isEnabled(this)
                    && AppSettings.TRANSPORT_BLE.equals(AppSettings.getTransport(this))) {
                requestBlePermissionsIfNeeded();
                if (isBleReady()) {
                    requestBatteryExemption();
                }
            } else if (AppSettings.isEnabled(this)) {
                requestBatteryExemption();
            }
            return;
        }
        if (requestCode != REQUEST_BLE_PERMISSIONS) {
            return;
        }
        blePermissionRequestInFlight = false;
        boolean granted = hasBlePermissions();
        if (!granted) {
            reportBleProblem("BLE 权限未授予，无法启动 BLE 转发");
            return;
        }
        if (transportSpinner.getSelectedItemPosition() != 1) {
            return;
        }
        requestBlePermissionsIfNeeded();
        if (isBleReady()) {
            requestBatteryExemption();
        }
        if (AppSettings.isEnabled(this)
                && AppSettings.TRANSPORT_BLE.equals(AppSettings.getTransport(this))) {
            startForwarder(ForwarderService.ACTION_START);
        }
    }

    private TextView label(String value) {
        TextView textView = text(value, 13, true);
        textView.setTextColor(0xFF334155);
        textView.setPadding(0, dp(14), 0, dp(4));
        return textView;
    }

    private LinearLayout flatSection(LinearLayout root, String index, String title) {
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        TextView heading = text(index + "  " + title, 20, true);
        heading.setTextColor(0xFF0F766E);
        heading.setPadding(0, dp(24), 0, dp(10));
        content.addView(heading);
        View divider = new View(this);
        divider.setBackgroundColor(0xFFD7DEE7);
        content.addView(divider, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(1)));
        content.setPadding(dp(4), 0, dp(4), dp(8));
        LinearLayout.LayoutParams contentParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        root.addView(content, contentParams);
        return content;
    }

    private TextView section(String value) {
        TextView textView = text(value, 18, true);
        textView.setTextColor(0xFF0F766E);
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
        textView.setTextColor(0xFF0F172A);
        textView.setTypeface(android.graphics.Typeface.create(
                bold ? "sans-serif-medium" : "sans-serif", 0));
        textView.setLineSpacing(0, 1.08f);
        return textView;
    }

    private EditText input(String hint) {
        EditText editText = new EditText(this);
        editText.setHint(hint);
        editText.setSingleLine(true);
        editText.setTextSize(16);
        editText.setTextColor(0xFF0F172A);
        editText.setHintTextColor(0xFF94A3B8);
        editText.setPadding(dp(13), dp(11), dp(13), dp(11));
        GradientDrawable background = rounded(0xFFF8FAFC, 10);
        background.setStroke(dp(1), 0xFFCBD5E1);
        editText.setBackground(background);
        return editText;
    }

    private Button button(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setAllCaps(false);
        button.setTextColor(0xFFFFFFFF);
        button.setTextSize(15);
        button.setBackgroundTintList(ColorStateList.valueOf(0xFF0F766E));
        button.setPadding(dp(14), dp(10), dp(14), dp(10));
        return button;
    }

    private GradientDrawable rounded(int color, int radiusDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(radiusDp));
        return drawable;
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
