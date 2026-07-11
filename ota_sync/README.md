# AMap ESP32 OTA Update Server

这个目录是给 `Amap-for-ESP32` 准备的独立 Node `update_server` 项目，风格参考 `amap-companion/update_server`，但服务内容改成了 ESP32 OTA 固件同步与分发。

它包含两部分职责：

1. 从 GitHub Release 或 GitHub Actions artifact 拉取 OTA 产物。
2. 通过本地 HTTP 服务暴露状态页、同步状态、手动同步接口和静态 OTA 文件目录。

## 目录结构

```text
ota_sync/
  public/
    index.html
    index.template.html
    ota/
      stable/
      dev/
  state/
    sync-state.json
  deploy/
    amap-ota-sync.service
    amap-ota-sync.timer
    amap-ota-update.service
  sync-core.mjs
  sync-build.mjs
  server.mjs
```

## 功能

- 支持 `stable` / `dev` 双渠道
- 默认同步 GitHub rolling Release：`ota-stable-latest`、`ota-dev-latest`
- 可切换为 GitHub Actions artifact 模式
- 校验 `manifest.json`、`firmware.bin`、`firmware.sha256`
- 先写固件，再写 manifest，避免设备读到半更新状态
- 自动把同步结果写入 `state/sync-state.json`
- 启动 HTTP 服务后提供：
  - `/health`
  - `/api/status`
  - `/api/channels`
  - `/api/manifest/stable`
  - `/api/manifest/dev`
  - `/sync`
  - `/ota/...`

## 依赖

- Node.js 20+

安装：

```bash
cd ota_sync
npm install
```

项目现在支持直接读取 `ota_sync/.env`。仓库里提供了：

- `.env.example`：可提交模板
- `.env`：本地默认配置，已经把端口设成 `8798`，避开服务器上你之前遇到的 `8788` 端口冲突

## 手动同步

默认把 OTA 文件写到 `ota_sync/public/ota/`：

```bash
cd ota_sync
npm run sync
```

也可以显式指定参数：

```bash
node sync-build.mjs \
  --repo zuo-qirun/Amap-for-ESP32 \
  --channels stable,dev \
  --source release \
  --web-root ./public/ota
```

如果你要从 artifact 拉取：

```bash
GITHUB_TOKEN=<github-token> node sync-build.mjs \
  --repo zuo-qirun/Amap-for-ESP32 \
  --channels dev \
  --source artifact \
  --web-root ./public/ota
```

## 启动 HTTP 服务

```bash
cd ota_sync
npm start
```

默认监听：

```text
http://0.0.0.0:8798
```

打开后可用接口：

- `/health`：服务健康与同步状态
- `/api/status`：完整状态 JSON
- `/api/channels`：每个 OTA 渠道摘要
- `/api/manifest/stable`
- `/api/manifest/dev`
- `/sync`：手动触发一次同步
- `/`：简易管理页

## 环境变量

```text
HOST=0.0.0.0
PORT=8798
AUTO_SYNC=1
SYNC_INTERVAL_MS=3600000

GITHUB_REPO=zuo-qirun/Amap-for-ESP32
GITHUB_TOKEN=<github-token>

OTA_SYNC_SOURCE=release
OTA_SYNC_CHANNELS=stable,dev
OTA_SYNC_STRICT_MISSING=0

OTA_WEB_ROOT=/opt/amap-ota-update-server/public/ota
OTA_SYNC_STATE_FILE=/opt/amap-ota-update-server/state/sync-state.json
PUBLIC_BASE_URL=https://ota.example.com
```

如果你已经使用仓库里的默认 `.env`，通常直接执行下面两条就够了：

```bash
cd ota_sync
npm run sync
npm start
```

说明：

- `AUTO_SYNC=1` 时，`server.mjs` 启动后会先同步一次，然后按 `SYNC_INTERVAL_MS` 定时同步。
- `OTA_SYNC_STRICT_MISSING=0` 时，如果某个渠道目前还没发布，会标记为 `skipped`，不会让整个同步失败。
- `PUBLIC_BASE_URL` 目前主要用于部署说明和后续扩展，不影响当前固件同步逻辑。

## 本地输出

同步完成后，默认目录结构为：

```text
ota_sync/public/ota/dev/manifest.json
ota_sync/public/ota/dev/firmware.bin
ota_sync/public/ota/dev/firmware.sha256
ota_sync/public/ota/stable/manifest.json
ota_sync/public/ota/stable/firmware.bin
ota_sync/public/ota/stable/firmware.sha256
ota_sync/state/sync-state.json
```

## systemd 示例

示例文件位于：

```text
deploy/amap-ota-update.service
deploy/amap-ota-sync.service
deploy/amap-ota-sync.timer
```

思路和 `amap-companion/update_server` 类似：

- `amap-ota-update.service`：启动 HTTP 服务
- `amap-ota-sync.service`：单次执行 `sync-build.mjs`
- `amap-ota-sync.timer`：每小时执行一次同步任务

如果你选择只用服务内定时同步，可以只启 `amap-ota-update.service`，把 `AUTO_SYNC=1` 打开。

如果你更喜欢外部定时器，也可以：

- 关闭 `AUTO_SYNC`
- 单独启用 `amap-ota-sync.timer`

## 建议部署方式

推荐像这样部署：

```text
/opt/amap-ota-update-server
```

然后：

1. `server.mjs` 监听本地端口，例如 `127.0.0.1:8788`
2. Nginx 反代 `/health`、`/api/...`、`/sync`
3. 静态 OTA 文件可由 Node 服务直接暴露，也可以由 Nginx 指向 `public/ota/`

如果你想和现有站点合并，常见方式是：

- `https://ota.zuoqirun.top/ota/...` 提供固件文件
- `https://ota.zuoqirun.top/api/...` 反代到 `server.mjs`
- `https://ota.zuoqirun.top/sync` 手动触发同步

## 和旧版 `ota_sync` 的区别

旧版更像一个纯同步脚本；现在这版已经升级为完整的 `update_server` 风格项目，便于你后续像 `amap-companion/update_server` 一样维护和部署。
