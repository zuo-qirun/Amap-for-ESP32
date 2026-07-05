# Android Forwarder

轻量 Android 转发 App，负责监听高德地图车机版广播、聚合导航状态，并通过 UDP 发送给 ESP32-S3。

## 模块

- `MainActivity.java`：设置页，控制转发开关、ESP32 IP、UDP 端口、测试帧和状态显示。
- `ForwarderService.java`：前台服务，动态注册高德广播并保持后台转发。
- `AMapBroadcastReceiver.java`：广播入口。
- `AMapStateAggregator.java`：维护完整导航状态快照。
- `LaneInfoParser.java`：把高德车道广播解析成 `lanes/advised` 数组。
- `TrafficLightParser.java`：把红绿灯倒计时解析成简化数组。
- `Esp32Protocol.java`：生成协议 v1 JSON。
- `Esp32UdpForwarder.java`：节流、心跳和 UDP 发送。
- `Esp32Transport.java` / `UdpTransport.java` / `BleTransport.java`：传输层抽象，BLE 目前预留。

## 监听的高德 action

```text
AUTONAVI_STANDARD_BROADCAST_SEND
AUTONAVI_STANDARD_BROADCAST_RECV
AUTO_GUIDE_INFO_FOR_INTERNAL_WIDGET
AUTO_STATUS_FOR_INTERNAL_WIDGET
com.autonavi.amapauto.AUTO_WIDGET_UPDATE_ROAD_NAME_INFO
com.autonavi.amapauto.AUTO_WIDGET_UPDATE_SILENCE_ROADNAME_INFO
com.autonavi.amapauto.AUTO_WIDGET_UPDATE_GPS_INFO
com.autonavi.amapauto.AUTO_WIDGET_UPDATE_CAR_DIRECTION
com.autonavi.amapauto.AUTO_WIDGET_UPDATE_CAMERA_INFO
com.autonavi.amapauto.AUTO_WIDGET_UPDATE_TRAFFIC_LIGHT_INFO
com.autonavi.amapauto.AUTO_WIDGET_UPDATE_CRUISE_TRAFFIC_LIGHT_INFO
```

## 构建

用 Android Studio 打开本目录并同步 Gradle。项目使用：

- Android Gradle Plugin `8.6.1`
- `compileSdk 36`，`targetSdk 35`
- 原生 Java Activity/Service，无 AndroidX 依赖

安装后先授予通知权限，再启用转发服务。服务是前台服务，不需要悬浮窗权限。

## 调试

1. 先在 App 内填写 ESP32 IP 和端口。
2. 点击“发送测试帧”。
3. ESP32 OLED 和串口应显示测试导航数据。
4. 再打开高德地图车机版导航或巡航，观察 App 内“最近广播 / 最近发送 / Payload / 最近错误”。

常见错误：

- `UnknownHost`：ESP32 IP 填写错误。
- `Network unreachable`：手机和 ESP32 不在同一网段，或热点客户端隔离。
- 选择 BLE 后报错：当前 BLE 只是预留接口，请切回 UDP。
