# AMap ESP32-S3 Navigation Display

这是一个独立项目，用轻量 Android 转发 App 监听高德地图车机版广播，聚合成完整导航状态快照，再通过 Wi-Fi UDP 发给 ESP32-S3。ESP32-S3 负责接收 JSON、维护硬件无关的导航状态，并在 OLED 上做压缩显示；该状态模型已为后续 SPI TFT 仪表界面预留。

## 许可证

本项目采用自定义非商用许可证，详见 [LICENSE](LICENSE)。

整个项目均禁止商用，包括 Android App、ESP32 固件、源代码、构建脚本、文档、JSON 协议说明、OLED UI 方案、硬件接线方案、配置方案以及后续加入的硬件设计资料。允许个人学习、研究、评估、非商业自用和非商业分享；任何商业用途都需要先取得作者的明确书面授权。

通用 ESP32 开发板、OLED 模块、线材等现成元器件本身不归本项目所有；但不得将本项目资料或其衍生作品用于商业产品、商业套件、付费部署、付费服务或营利组织的业务用途。

## 技术方案

### 总体架构

```text
android_forwarder/  手机端轻量转发 App
esp32_display/      ESP32-S3 PlatformIO + Arduino OLED 显示端
```

Android 端不做悬浮窗、不复用 `WindowManager` UI，只复用高德广播 action、keyType、车道和红绿灯解析思路。ESP32 端不依赖增量包，每次收到的 UDP 都是完整状态快照。

### 已验证硬件

本工程按同硬件参考项目 `3D-Printer-Filament-Dryer` 对齐：

- ESP32-S3 N16R8：PlatformIO `esp32-s3-n16r8`（16 MB Flash / 8 MB OPI PSRAM）
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
3. `NavState` 保存当前导航、车道、红绿灯、电子眼、ETA、速度、路线进度、目的地、道路类型/路况、出口及服务区等状态。
4. `DisplayRenderer` 按 OLED 高度压缩显示，128x64 优先显示模式、道路、转向、距离、下一道路、ETA，底部轮播车道/红绿灯/电子眼/路线与道路提醒。

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
  "tmc": {
    "totalDistance": 28600,
    "finishDistance": 16300,
    "segments": [
      {"status": 10, "distance": 4200},
      {"status": 1, "distance": 8700},
      {"status": 2, "distance": 3900},
      {"status": 3, "distance": 5200}
    ]
  },
  "route": {
    "remainingMeters": 12300,
    "totalMeters": 28600,
    "remainingSeconds": 1080,
    "progressPercent": 57,
    "destination": "目的地",
    "remainingTrafficLights": 4
  },
  "roadInfo": {
    "type": "高速",
    "bearing": 90,
    "traffic": "前方畅通",
    "crossMap": false
  },
  "guide": {
    "exitName": "学院路出口",
    "exitDirection": "靠右驶离",
    "serviceAreaName": "清河服务区",
    "serviceAreaDistance": "8.6公里"
  },
  "alert": "前方测速摄像头",
  "detail": ""
}
```

### TMC 路况

Android 端会按 `KEY_TYPE=13011` 解析 `EXTRA_TMC_SEGMENT`，兼容 `total_distance` / `finish_distance` / `tmc_info` 及 camelCase 变体。为保证 UDP 帧稳定，最多转发 8 个分段；超过的尾部区间会合并。状态颜色与 AMap Companion、Navi-Link 的映射一致：`10` 灰（已驶过）、`1` 绿（畅通）、`2` 黄（缓行）、`3` 红（拥堵）、`4` 深红（严重拥堵）、`0` 蓝、`5` 青蓝。开发者 TFT 模拟界面会绘制同样的分段色条和当前位置标记。

### 4 MB Flash 与 TFT 资源

当前 ESP32-S3 N16R8 的 16 MB 双 OTA 分区中，每个 app 槽为 5 MB，当前固件约为 1.23 MB；另有约 5.9 MB SPIFFS 可用于可选的大型字体或位图资源。网页预览与 TMC 色条目前全部由 CSS/几何图形绘制，不占资源文件空间；TFT 也应优先用几何绘制、少量图标和按需字体。若以后需要兼容 4 MB Flash，应取消大资源并改为较小的 OTA 分区方案。

## 快速开始

### 1. 配置 ESP32

编辑 `esp32_display/include/Config.h`：

- `AMAP_WIFI_SSID` / `AMAP_WIFI_PASSWORD`：车机热点或共享 AP。
- `AMAP_UDP_PORT`：默认 `4210`。
- `AMAP_OLED_SDA_PIN` / `AMAP_OLED_SCL_PIN`：默认 `GPIO8/GPIO9`。
- `AMAP_OLED_I2C_ADDRESS`：默认 `0x78`。
- `AMAP_OLED_DRIVER`：默认 `AMAP_OLED_DRIVER_SSD1306_12864`。
- `AMAP_TFT_*`：为 ST7789 / ILI9341 SPI TFT 保留的驱动、分辨率和引脚配置；在确定屏幕型号与接线前保持默认 `-1` 引脚，不会影响当前 OLED。

然后编译上传：

```powershell
cd esp32_display
pio run
pio run -t upload
pio device monitor
```

串口会打印 I2C 扫描结果、ESP32 IP，以及收到 UDP 后解析出的主要字段。若 OLED 不亮，先看串口是否扫到 `addr7=0x3C addr8=0x78`。

如果 `Config.h` 中的 Wi-Fi 仍是占位符，或 ESP32 无法连接目标 Wi-Fi，设备会自动进入 AP 配网模式：

- 手机连接 `AMap-ESP32-xxxxxx` 热点。
- 系统通常会自动弹出配网页面；没有弹出时手动访问 `http://192.168.4.1/`。
- 页面会显示当前 Wi-Fi 状态、STA IP、UDP 端口、配网热点状态，并可保存新的 Wi-Fi 名称和密码。
- 配网保存后 ESP32 会自动重连；连接成功后配网热点会关闭。
- 已连接 Wi-Fi 后，也可以通过 ESP32 串口打印的 STA IP 访问同一个状态/配网页面。
- 配置页的“开发者选项”可开启 SPI TFT 模拟显示：严格渲染最近一次成功解析的 UDP JSON；无有效帧时显示等待状态。开关会保存在 NVS，且不会改变 OLED 实际输出。

### OTA 升级

ESP32 固件支持 dev/stable 双渠道 OTA。设备只访问你配置的国内 OTA 服务器，不直接访问 GitHub。

- GitHub Actions 会在 push 到 `dev` 时构建 dev 固件，在 push 到 `main` 或 `master` 时构建 stable 固件。
- CI 产物包含 `firmware.bin`、`firmware.sha256`、`manifest.json`、`CHANGELOG.md`，同时上传为 artifact，并发布到滚动 Release：`ota-dev-latest` / `ota-stable-latest`。默认版本和构建号使用提交历史计数；同一提交无论从 `dev` 还是 `main`/`master` 构建，都会得到相同版本号。
- 国内服务器可用 `scripts/sync_ota_from_github.py` 从 GitHub artifact 或 Release 拉取产物，落盘到宝塔站点 `/www/wwwroot/ota.zuoqirun.top/ota/dev/` 和 `/www/wwwroot/ota.zuoqirun.top/ota/stable/`。
- ESP32 联网后按 `OTA_BASE_URL` + `OTA_CHANNEL` 请求 manifest；发现新版本后只在配置页面提示，不会自动升级。
- 在配置页面点击“检查更新”后可看到当前版本、当前渠道、最新版本、构建信息、更新日志和错误信息；设备会优先显示 `CHANGELOG.md`，读取失败时回退到 manifest 内的简短说明。点击“立即升级”才会下载 `firmware.bin`、校验 SHA256 并写入 OTA 分区。

示例服务器目录：

```text
/www/wwwroot/ota.zuoqirun.top/ota/dev/manifest.json
/www/wwwroot/ota.zuoqirun.top/ota/dev/firmware.bin
/www/wwwroot/ota.zuoqirun.top/ota/dev/CHANGELOG.md
/www/wwwroot/ota.zuoqirun.top/ota/stable/manifest.json
/www/wwwroot/ota.zuoqirun.top/ota/stable/firmware.bin
/www/wwwroot/ota.zuoqirun.top/ota/stable/CHANGELOG.md
```

当前 OTA 服务器按宝塔面板独立站点管理：`ota.zuoqirun.top`，站点根目录 `/www/wwwroot/ota.zuoqirun.top`，ESP32 默认 OTA 根地址为 `http://ota.zuoqirun.top/ota`。DNS 解析生效并在宝塔为该站点申请 SSL 后，可切换为 `https://ota.zuoqirun.top/ota`。

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

## 非商业声明

本仓库公开不代表授予商用权。禁止将本项目或其衍生作品用于销售、预装、打包硬件套件、付费安装维护、商业部署、营利组织内部业务或其他商业利益相关场景。
