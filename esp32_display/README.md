# ESP32 Display

ESP32-S3 PlatformIO + Arduino 工程，接收 Android App 发来的 UDP 或 BLE JSON，并在 OLED/TFT 上显示导航信息。

## 许可证

本 ESP32 子项目跟随仓库根目录 [LICENSE](../LICENSE)，禁止商用。禁止将固件、源码、二进制、OLED UI、接线方案、配置方案或衍生硬件资料用于销售、预装、商业套件、付费部署、付费服务或营利组织业务用途。

## 硬件基线

本工程已按同硬件参考项目 `3D-Printer-Filament-Dryer` 对齐：

- PlatformIO 环境：`esp32-s3-n16r8`（16 MB Flash / 8 MB OPI PSRAM）
- OLED：SSD1306 0.96" I2C，128x64
- OLED 地址：8-bit `0x78`，等效 7-bit `0x3C`
- I2C SDA：GPIO8
- I2C SCL：GPIO9

## 配置

编辑 `include/Config.h`：

```cpp
#define AMAP_WIFI_SSID "car_hotspot_ssid"
#define AMAP_WIFI_PASSWORD "car_hotspot_password"
#define OTA_BASE_URL "http://ota.zuoqirun.top/ota"
#define OTA_CHANNEL AMAP_FIRMWARE_CHANNEL
#define AMAP_UDP_PORT 4210
#define AMAP_OLED_SDA_PIN 8
#define AMAP_OLED_SCL_PIN 9
#define AMAP_OLED_I2C_ADDRESS 0x78
#define AMAP_OLED_DRIVER AMAP_OLED_DRIVER_SSD1306_12864
```

`AMAP_WIFI_SSID` / `AMAP_WIFI_PASSWORD` 是兜底配置。设备也支持 Web 配网，保存后的 Wi-Fi 会写入 ESP32 NVS，优先级高于 `Config.h`。Web 配置页会随设备启动常驻；当 ESP32 已连上外部 Wi-Fi 时，可直接访问 `http://设备的 STA IP/`。

## BLE 通信

- 固件启动后会广播 `AMap-ESP32-XXXXXX`，无需配对。
- GATT 服务 UUID：`6e400001-b5a3-f393-e0a9-e50e24dcca9e`。
- 写入特征 UUID：`6e400002-b5a3-f393-e0a9-e50e24dcca9e`。
- Android 会按协商 MTU 分片发送完整 JSON；ESP32 校验帧号、偏移和总长度后重组，最大包长为 `AMAP_PACKET_BUFFER_SIZE`。
- BLE 可以在没有 Wi-Fi 的情况下驱动屏幕；Wi-Fi 仍可同时用于配置页与 OTA。

## AP 配网页面

当未配置 Wi-Fi、Wi-Fi 连接超时或 Wi-Fi 不可用时，ESP32 会自动进入 AP 配网模式：

1. 手机连接热点 `AMap-ESP32-xxxxxx`。
2. 系统通常会自动弹出配网页面；若没有弹出，手动访问 `http://192.168.4.1/`。
3. 页面会显示 Wi-Fi 状态、当前 SSID、配置来源、STA IP、UDP 端口、配网热点状态。
4. 输入新的 Wi-Fi 名称和密码，点击“保存并连接”。
5. 连接成功后配网热点会关闭；之后仍可通过串口打印的 STA IP 访问同一个状态/配网页面。

页面还提供“清除保存的 Wi-Fi”，清除后会回退到 `Config.h` 兜底配置；如果兜底 SSID 仍是占位符，则继续保持配网模式。

配网热点默认不设置密码，便于手机自动弹出 captive portal；如需加密，可把 `AMAP_CONFIG_AP_PASSWORD` 改为 8 位以上密码。

OLED 驱动默认支持：

- `AMAP_OLED_DRIVER_SSD1306_12864`
- `AMAP_OLED_DRIVER_SH1106_12864`
- `AMAP_OLED_DRIVER_SH1107_128128`

其他 128x128、128x160 或特殊控制器 OLED，可以在 `src/DisplayRenderer.cpp` 中替换 U8g2 构造器，保留渲染逻辑即可。

## OTA 升级

### 渠道

- `dev`：push 到 `dev` 分支时由 GitHub Actions 自动构建，适合测试新功能。
- `stable`：push 到 `main` 或 `master` 分支时自动构建，适合日常使用。

`platformio.ini` 默认本地构建为 `dev`：

```ini
-D AMAP_FIRMWARE_VERSION=\"0.1.0\"
-D AMAP_FIRMWARE_BUILD=1
-D AMAP_FIRMWARE_CHANNEL=\"dev\"
```

CI 会覆盖这些宏。ESP32 通过 `Config.h` 中的 `OTA_BASE_URL` 和 `OTA_CHANNEL` 选择对应 manifest：

```text
{OTA_BASE_URL}/{OTA_CHANNEL}/manifest.json
{OTA_BASE_URL}/{OTA_CHANNEL}/firmware.bin
```

### GitHub Actions 构建

工作流文件为 `.github/workflows/esp32-ota-build.yml`。每次 push 到 `dev`、`main`、`master` 都会：

1. 使用 PlatformIO 构建 `esp32_display/` 的 `esp32-s3-n16r8` 环境。
2. 生成 `dist/{channel}/firmware.bin`。
3. 生成 `firmware.sha256`。
4. 生成 `manifest.json` 和 `CHANGELOG.md`。
5. 上传 GitHub Actions artifact。
6. 发布滚动 GitHub Release：`ota-dev-latest` 或 `ota-stable-latest`。

默认的 `version` 与 `build_number` 使用提交历史计数，因此同一提交从 `dev` 和 `main`/`master` 构建时始终一致；如有发布需求，也可以通过 `OTA_VERSION` 与 `OTA_BUILD_NUMBER` 覆盖。

manifest 示例：

```json
{
  "channel": "stable",
  "version": "0.1.42",
  "build_number": 42,
  "git_branch": "master",
  "git_commit": "abcdef123456...",
  "build_time": "2026-07-09T12:00:00+00:00",
  "firmware_url": "firmware.bin",
  "sha256": "64 hex chars",
  "size": 1183381,
  "min_supported_version": "0.1.0",
  "release_notes": "stable build 0.1.42",
  "changelog_url": "CHANGELOG.md"
}
```

`firmware_url` 与 `changelog_url` 都可以是绝对 URL，也可以是 `firmware.bin`、`CHANGELOG.md` 这类相对路径。相对路径会解析为当前渠道目录。设备检查更新时会读取 `CHANGELOG.md` 并显示在配置页面；无法读取时仍会显示 `release_notes`，且不影响 OTA 升级。

### 国内服务器同步

GitHub Actions 不主动上传到服务器。推荐在国内服务器部署仓库里的独立 Node 项目 `ota_sync/`，定时从 GitHub Release 或 artifact 拉取产物：

```bash
cd ota_sync
npm install

# 同步 stable + dev，默认每个渠道写入 /ota/{channel}/
node index.mjs \
  --repo zuo-qirun/Amap-for-ESP32 \
  --channels stable,dev \
  --source release \
  --web-root /www/wwwroot/ota.zuoqirun.top/ota

# 如果要从 Actions artifact 同步 dev，需要 token
GITHUB_TOKEN=<github-token> node index.mjs \
  --repo zuo-qirun/Amap-for-ESP32 \
  --channels dev \
  --source artifact \
  --web-root /www/wwwroot/ota.zuoqirun.top/ota
```

宝塔服务器推荐每小时执行一次：

```cron
0 * * * * flock -xn /tmp/amap-ota-sync.lock -c 'cd /opt/amap-ota-sync && /usr/bin/node index.mjs --repo zuo-qirun/Amap-for-ESP32 --channels stable,dev --source release --web-root /www/wwwroot/ota.zuoqirun.top/ota' >> /var/log/amap-ota-sync.log 2>&1
```

仓库根目录中的 `scripts/sync_ota_from_github.py` 和 `scripts/sync_ota_from_github.sh` 仍可作为备用示例，但当前推荐维护 Node 版本。

同步后的目录：

```text
/www/wwwroot/ota.zuoqirun.top/ota/dev/manifest.json
/www/wwwroot/ota.zuoqirun.top/ota/dev/firmware.bin
/www/wwwroot/ota.zuoqirun.top/ota/dev/firmware.sha256
/www/wwwroot/ota.zuoqirun.top/ota/dev/CHANGELOG.md
/www/wwwroot/ota.zuoqirun.top/ota/stable/manifest.json
/www/wwwroot/ota.zuoqirun.top/ota/stable/firmware.bin
/www/wwwroot/ota.zuoqirun.top/ota/stable/firmware.sha256
/www/wwwroot/ota.zuoqirun.top/ota/stable/CHANGELOG.md
```

ESP32 只需要访问国内服务器，例如：

```cpp
#define OTA_BASE_URL "http://ota.zuoqirun.top/ota"
#define OTA_CHANNEL "stable"
```

当前服务器使用宝塔面板管理，独立站点为 `ota.zuoqirun.top`，站点根目录为 `/www/wwwroot/ota.zuoqirun.top`。DNS 解析生效前可以先完成站点和同步脚本部署；解析生效后建议在宝塔面板为该站点申请 SSL，再把 `OTA_BASE_URL` 切换为 `https://ota.zuoqirun.top/ota`。

### 手动升级流程

1. ESP32 联网后会自动请求当前渠道 manifest。
2. 如果 manifest 的 `build_number` 更大，或 build 相同但 `version` 更高，配置页面会显示有新版本。
3. 打开配置页面，可在 `stable` / `dev` 间选择更新渠道；点击“检查更新”可立即刷新所选渠道的 manifest。
4. 点击“立即升级”后，ESP32 下载 `firmware.bin`。
5. 固件下载完成后校验 SHA256。
6. 校验通过后通过 Arduino `Update` 写入 OTA 分区。
7. 写入成功后自动重启。

升级失败时，配置页面会显示 HTTP 错误、manifest 错误、大小不匹配、SHA256 校验失败或 `Update` 写入错误。ESP32 会自动跟随 manifest / firmware 的常见重定向（301/302/303/307/308），适用于站点将 `http` 跳转到 `https` 的场景。

### dev 回退 stable

本工程已经使用 16MB Flash 双 OTA 分区表 `partitions_ota_16mb.csv`：

```text
app0 0x500000
app1 0x500000
```

当前固件约 1.2MB，适配该 OTA 分区。

Arduino + PlatformIO 当前最小实现使用应用层 fallback：

- NVS 记录当前版本、build、渠道、启动次数、是否完成健康检查。
- dev 固件启动后，需要在 Wi-Fi 已连接、WebServer 已启动、OLED 初始化完成，并经过 `OTA_HEALTHY_MARK_DELAY_MS` 后标记 healthy。
- 若 dev 固件连续多次未完成健康检查，且之后具备联网条件，会尝试从 stable manifest 下载稳定版并覆盖升级。

限制：如果固件在应用层逻辑运行前就 panic、死循环或无法初始化到 OTA 逻辑，应用层 fallback 无法执行；这种场景只有 ESP32 bootloader rollback 机制才能可靠恢复。当前 Arduino/PlatformIO 配置未完整启用 bootloader rollback，后续若要加强，需要切到 ESP-IDF/Arduino 混合配置并启用相关 bootloader rollback 选项。

## 构建上传

```powershell
cd esp32_display
pio run -e esp32-s3-n16r8
pio run -e esp32-s3-n16r8 -t upload
pio device monitor --baud 115200
```

串口输出示例：

```text
AMap ESP32-S3 Navigation Display
I2C scan on SDA=8 SCL=9, OLED addr8=0x78 addr7=0x3C
I2C device found: addr7=0x3C addr8=0x78
UDP 192.168.1.23:54231 seq=1 mode=nav road=京藏高速 turn=右转 dist=300米
```

如果 OLED 不亮：

1. 串口确认是否扫到 `addr7=0x3C addr8=0x78`。
2. 确认 VCC/GND/SDA/SCL 接线，SDA 为 GPIO8，SCL 为 GPIO9。
3. 确认屏幕控制器是 SSD1306；若是 SH1106，改 `AMAP_OLED_DRIVER_SH1106_12864`。

## OLED 显示策略

128x64 会优先显示：

1. 模式和当前道路
2. 转向方向和距离
3. 下一道路
4. ETA、剩余距离、当前速度
5. 底部轮播：红绿灯、电子眼、车道、提醒、detail

更高屏幕会额外显示提醒和 detail 行。

## 超时行为

- Wi-Fi 未连接或不可用：自动开启 AP 配网模式，OLED 显示 `AP 192.168.4.1`。
- 3 秒未收到 Android 数据：显示“等待手机数据”。
- 10 秒未收到 Android 数据：显示待机页。

## 中文字体

`Config.h` 中 `AMAP_USE_CHINESE_FONT` 默认为 `1`，使用 U8g2 的 CJK 字体显示中文道路名。若固件体积太大或只显示英文/数字，可改为 `0`。
