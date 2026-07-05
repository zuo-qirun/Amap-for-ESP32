# AMap ESP32-S3 Navigation Display

这是一个独立项目，用轻量 Android 转发 App 监听高德地图车机版广播，聚合成完整导航状态快照，再通过 Wi-Fi UDP 发给 ESP32-S3。ESP32-S3 只负责接收 JSON、维护当前状态，并在 OLED 上做压缩显示。

## 技术方案

### 总体架构

```text
android_forwarder/  手机端轻量转发 App
esp32_display/      ESP32-S3 PlatformIO + Arduino OLED 显示端
```

Android 端不做悬浮窗、不复用 `WindowManager` UI，只复用高德广播 action、keyType、车道和红绿灯解析思路。ESP32 端不依赖增量包，每次收到的 UDP 都是完整状态快照。

### 已验证硬件

本工程按同硬件参考项目 `3D-Printer-Filament-Dryer` 对齐：

- ESP32-S3：PlatformIO `esp32-s3-devkitm-1`
- OLED：0.96" SSD1306 I2C，128x64
- OLED 地址：8-bit `0x78`，等效 7-bit `0x3C`
- I2C SDA：GPIO8
- I2C SCL：GPIO9
- U8g2 初始化：`Wire.begin(8, 9)` + `setI2CAddress(0x78)` + `U8G2_SSD1306_128X64_NONAME_F_HW_I2C`

### Android 数据流

1. `ForwarderService` 启动前台服务，动态注册高德车机版广播。
2. `AMapBroadcastReceiver` 收到广播后交给 `AMapStateAggregator`。
3. `AMapStateAggregator` 按字段增量更新本地完整状态，但输出完整 `Esp32NavState` 快照。
4. `Esp32Protocol` 把快照序列化为协议 v1 JSON。
5. `Esp32UdpForwarder` 节流发送，关键变化立即发送，每 1 秒发送一次完整心跳快照。

### ESP32 数据流

1. `NetworkManager` 连接车机热点或共享 AP，并监听 UDP `4210`。
2. `ProtocolParser` 用 ArduinoJson 解析 Android 发来的完整 JSON。
3. `NavState` 保存当前导航、车道、红绿灯、电子眼、ETA、速度等状态。
4. `DisplayRenderer` 按 OLED 高度压缩显示，128x64 优先显示模式、道路、转向、距离、下一道路、ETA，底部轮播车道/红绿灯/电子眼/提醒。

### UDP 与降级策略

- UDP 不做 ACK，Android 始终发完整快照，ESP32 丢包后靠下一帧恢复。
- ESP32 超过 3 秒未收到包会显示“等待手机数据”。
- ESP32 超过 10 秒未收到包会回到待机页。
- 如果手机和 ESP32 同接车机热点但无法互通，优先检查热点客户端隔离；代码已预留 `Esp32Transport` / `BleTransport`，后续可以补 BLE GATT RX。

### JSON 协议

协议版本为 `proto=1`，帧类型固定为 `nav_state`。所有字段都是完整快照。

```json
{
  "proto": 1,
  "type": "nav_state",
  "seq": 1,
  "ts": 1760000000000,
  "active": true,
  "mode": "nav",
  "road": "京藏高速",
  "heading": "北",
  "turn": {
    "icon": 3,
    "text": "右转",
    "distanceText": "300米",
    "distanceMeters": 300,
    "road": "学院路"
  },
  "eta": {
    "remainDistanceText": "12.3公里",
    "remainTimeText": "18分钟",
    "arriveTimeText": "14:35"
  },
  "speed": {
    "current": 63,
    "limit": 80,
    "overspeedLevel": 0
  },
  "lane": {
    "lanes": [1, 4, 4, 2],
    "advised": [false, true, true, false]
  },
  "lights": [
    {"dir": 4, "status": 1, "seconds": 18}
  ],
  "camera": {
    "type": 1,
    "distance": 350,
    "speedLimit": 80
  },
  "alert": "前方测速摄像头",
  "detail": ""
}
```

## 快速开始

### 1. 配置 ESP32

编辑 `esp32_display/include/Config.h`：

- `AMAP_WIFI_SSID` / `AMAP_WIFI_PASSWORD`：车机热点或共享 AP。
- `AMAP_UDP_PORT`：默认 `4210`。
- `AMAP_OLED_SDA_PIN` / `AMAP_OLED_SCL_PIN`：默认 `GPIO8/GPIO9`。
- `AMAP_OLED_I2C_ADDRESS`：默认 `0x78`。
- `AMAP_OLED_DRIVER`：默认 `AMAP_OLED_DRIVER_SSD1306_12864`。

然后编译上传：

```powershell
cd esp32_display
pio run
pio run -t upload
pio device monitor
```

串口会打印 I2C 扫描结果、ESP32 IP，以及收到 UDP 后解析出的主要字段。若 OLED 不亮，先看串口是否扫到 `addr7=0x3C addr8=0x78`。

### 2. 配置 Android

用 Android Studio 打开 `android_forwarder/`，安装到手机或车机侧 Android 设备。

App 页面中设置：

- 通信方式：选择 `UDP`。
- ESP32 IP：填写 ESP32 串口打印出的本机 IP。
- UDP 端口：默认 `4210`，需要与 ESP32 一致。
- 打开“启用转发服务”。
- 点击“发送测试帧”，ESP32 OLED 应显示测试导航信息。

### 3. 车机热点检查

推荐闭环：

1. 车机开启热点。
2. 手机连接车机热点。
3. ESP32 连接同一个热点。
4. ESP32 串口确认 IP。
5. Android App 填写该 IP 并发送测试帧。
6. 如果 ESP32 没收到，检查车机热点是否开启“客户端隔离”。

## 子项目说明

- Android 端详情见 `android_forwarder/README.md`。
- ESP32 端详情见 `esp32_display/README.md`。
