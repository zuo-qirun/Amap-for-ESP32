# ESP32 Display

ESP32-S3 PlatformIO + Arduino 工程，接收 Android App 发来的 UDP JSON，并用 U8g2 在 OLED 上显示导航信息。

## 许可证

本 ESP32 子项目跟随仓库根目录 [LICENSE](../LICENSE)，禁止商用。禁止将固件、源码、二进制、OLED UI、接线方案、配置方案或衍生硬件资料用于销售、预装、商业套件、付费部署、付费服务或营利组织业务用途。

## 硬件基线

本工程已按同硬件参考项目 `3D-Printer-Filament-Dryer` 对齐：

- PlatformIO 环境：`esp32-s3-devkitm-1`
- OLED：SSD1306 0.96" I2C，128x64
- OLED 地址：8-bit `0x78`，等效 7-bit `0x3C`
- I2C SDA：GPIO8
- I2C SCL：GPIO9

## 配置

编辑 `include/Config.h`：

```cpp
#define AMAP_WIFI_SSID "car_hotspot_ssid"
#define AMAP_WIFI_PASSWORD "car_hotspot_password"
#define AMAP_UDP_PORT 4210
#define AMAP_OLED_SDA_PIN 8
#define AMAP_OLED_SCL_PIN 9
#define AMAP_OLED_I2C_ADDRESS 0x78
#define AMAP_OLED_DRIVER AMAP_OLED_DRIVER_SSD1306_12864
```

OLED 驱动默认支持：

- `AMAP_OLED_DRIVER_SSD1306_12864`
- `AMAP_OLED_DRIVER_SH1106_12864`
- `AMAP_OLED_DRIVER_SH1107_128128`

其他 128x128、128x160 或特殊控制器 OLED，可以在 `src/DisplayRenderer.cpp` 中替换 U8g2 构造器，保留渲染逻辑即可。

## 构建上传

```powershell
cd esp32_display
pio run -e esp32-s3-devkitm-1
pio run -e esp32-s3-devkitm-1 -t upload
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

- Wi-Fi 未连接：显示“连接 WiFi 中”。
- 3 秒未收到 Android 数据：显示“等待手机数据”。
- 10 秒未收到 Android 数据：显示待机页。

## 中文字体

`Config.h` 中 `AMAP_USE_CHINESE_FONT` 默认为 `1`，使用 U8g2 的 CJK 字体显示中文道路名。若固件体积太大或只显示英文/数字，可改为 `0`。
