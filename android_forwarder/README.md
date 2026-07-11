# Android Forwarder

轻量 Android 转发 App，负责监听高德地图车机版广播、聚合导航状态，并通过 UDP 或 BLE 发送给 ESP32-S3。

## 许可证

本 Android 子项目跟随仓库根目录 [LICENSE](../LICENSE)，禁止商用。禁止将 App 源码、APK、衍生版本、转发服务或相关协议实现用于销售、预装、付费部署、付费服务或营利组织业务用途。

## 模块

- `MainActivity.java`：设置页，控制目标高德应用、UDP/BLE、测试帧和状态显示。
- `ForwarderService.java`：前台服务，动态注册高德广播并保持后台转发。
- `AMapBroadcastReceiver.java`：广播入口。
- `AMapStateAggregator.java`：维护完整导航状态快照。
- `LaneInfoParser.java`：把高德车道广播解析成 `lanes/advised` 数组。
- `TrafficLightParser.java`：把红绿灯倒计时解析成简化数组。
- `ServiceAreaParser.java`：兼容 `SAPA_*`、数组及 JSON 形式的当前/下一服务区信息。
- `Esp32Protocol.java`：生成协议 v1 JSON。
- `Esp32UdpForwarder.java`：节流、心跳和 UDP 发送。
- `Esp32Transport.java` / `UdpTransport.java` / `BleTransport.java`：UDP/BLE 传输层；BLE 自动扫描开发板并协商 MTU。
- `BlePacketFramer.java`：把完整 JSON 切成带帧号、偏移和首尾标记的 GATT 分片。

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

安装后授予通知权限；使用 BLE 时还需授予“附近设备”权限。服务是前台服务，不需要悬浮窗权限。

## 调试

1. UDP 模式填写 ESP32 IP 和端口；BLE 模式无需填写 IP，也无需提前配对。
2. 点击“发送测试帧”。
3. ESP32 OLED 和串口应显示测试导航数据。
4. 再打开高德地图车机版导航或巡航，观察 App 内“最近广播 / 最近发送 / Payload / 最近错误”。

## 巡航红绿灯与服务区数据来源

- 导航态红绿灯和服务区来自高德的 `AUTONAVI_STANDARD_BROADCAST_SEND` 广播。
- 巡航态红绿灯属于高德内部 `CameraLightInfoWrapper`，原版通常不会完整对外广播。需使用
  `amap-cruise-wrapper-skill` 修改对应版本的高德车机 APK，使其发送 `lightsData`、
  `lightsCount`、`clearLights`；转发器会将 wrapper 的 `0=红、1=绿` 归一化为 ESP32 协议状态。
- 服务区优先读取 `SAPA_NAME/SAPA_DIST(_AUTO)` 和 `NEXT_SAPA_*`，也兼容
  `SAPA_LIST`、`serviceAreas`、名称/距离数组以及 JSON 列表。收到空列表时会清除旧信息。
- ESP32 收到完整快照后，巡航页显示最多四路红绿灯；导航页底部显示最近服务区，文本轮播中还会显示下一服务区。

常见错误：

- `UnknownHost`：ESP32 IP 填写错误。
- `Network unreachable`：手机和 ESP32 不在同一网段，或热点客户端隔离。
- `未找到 AMap-ESP32 BLE 设备`：确认开发板已刷入支持 BLE 的固件、蓝牙已开启且距离足够近。
- `请授予附近设备权限`：在系统应用权限中允许本 App 使用附近设备。
