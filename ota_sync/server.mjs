#!/usr/bin/env node

import fs from "node:fs";
import fsp from "node:fs/promises";
import http from "node:http";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { loadEnv } from "./load-env.mjs";
import { buildConfig, formatError, readState } from "./sync-core.mjs";

const baseDir = path.dirname(fileURLToPath(import.meta.url));
loadEnv(baseDir);
const config = buildConfig([], baseDir);
const host = process.env.HOST || "0.0.0.0";
const port = Number(process.env.PORT || 8788);
const autoSyncEnabled = process.env.AUTO_SYNC !== "0";
const syncIntervalMs = Math.max(60_000, Number(process.env.SYNC_INTERVAL_MS || 3_600_000));
const publicDir = path.join(baseDir, "public");
const syncScriptPath = path.join(baseDir, "sync-build.mjs");
let syncing = false;
let lastSync = null;

async function ensurePublicFiles() {
  await fsp.mkdir(publicDir, { recursive: true });
  const indexPath = path.join(publicDir, "index.html");
  const templatePath = path.join(publicDir, "index.template.html");
  if (fs.existsSync(templatePath)) {
    await fsp.copyFile(templatePath, indexPath);
  }
}

function sendJson(res, statusCode, body) {
  const text = JSON.stringify(body, null, 2);
  res.writeHead(statusCode, {
    "content-type": "application/json; charset=utf-8",
    "cache-control": "no-store",
    "access-control-allow-origin": "*",
  });
  res.end(text);
}

function sendText(res, statusCode, text) {
  res.writeHead(statusCode, {
    "content-type": "text/plain; charset=utf-8",
    "cache-control": "no-store",
  });
  res.end(text);
}

function sendFile(res, filePath, contentType) {
  if (!fs.existsSync(filePath) || !fs.statSync(filePath).isFile()) {
    sendText(res, 404, "not found");
    return;
  }
  res.writeHead(200, {
    "content-type": contentType,
    "content-length": fs.statSync(filePath).size,
    "cache-control": "no-store",
  });
  fs.createReadStream(filePath).pipe(res);
}

async function readRequestJson(req, limit = 32 * 1024) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on("data", (chunk) => {
      size += chunk.length;
      if (size > limit) {
        reject(new Error("request too large"));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on("error", reject);
    req.on("end", () => {
      try {
        const text = Buffer.concat(chunks).toString("utf8").trim();
        resolve(text ? JSON.parse(text) : {});
      } catch {
        reject(new Error("invalid json"));
      }
    });
  });
}

async function manifestForChannel(channel) {
  const filePath = path.join(config.webRoot, channel, "manifest.json");
  const raw = await fsp.readFile(filePath, "utf8");
  return JSON.parse(raw);
}

function publicChannelState(channelState) {
  if (!channelState) {
    return null;
  }
  return {
    status: channelState.status || "unknown",
    syncedAt: channelState.syncedAt || null,
    version: channelState.version || "",
    buildNumber: channelState.buildNumber || 0,
    gitBranch: channelState.gitBranch || "",
    gitCommit: channelState.gitCommit || "",
    buildTime: channelState.buildTime || "",
    releaseNotes: channelState.releaseNotes || "",
    lastError: publicErrorMessage(channelState.lastError || "", channelState.status || "unknown"),
  };
}

function publicErrorMessage(message, status = "unknown") {
  const text = String(message || "").trim();
  if (!text) {
    return "";
  }
  const lowered = text.toLowerCase();
  if (status === "skipped") {
    return "Channel not published yet.";
  }
  if (lowered.includes("missing assets") || lowered.includes("missing field")) {
    return "Required OTA files are incomplete.";
  }
  if (lowered.includes("sha256 mismatch")) {
    return "Firmware hash verification failed.";
  }
  if (lowered.includes("size mismatch")) {
    return "Firmware size verification failed.";
  }
  if (lowered.includes("http ") || lowered.includes("fetch failed") || lowered.includes("curl exited")) {
    return "Upstream sync request failed.";
  }
  if (lowered.includes("downloaded payload missing")) {
    return "Downloaded OTA payload is incomplete.";
  }
  return "Sync failed. Check server logs for details.";
}

function publicRunState(runState) {
  if (!runState) {
    return null;
  }
  return {
    startedAt: runState.startedAt || null,
    finishedAt: runState.finishedAt || null,
    synced: Number(runState.synced || 0),
    skipped: Number(runState.skipped || 0),
    failed: Number(runState.failed || 0),
  };
}

function publicLastSync(syncState) {
  if (!syncState) {
    return null;
  }
  return {
    reason: syncState.reason || "",
    startedAt: syncState.startedAt || null,
    finishedAt: syncState.finishedAt || null,
  };
}

function publicState(state) {
  return {
    generatedAt: state.generatedAt || null,
    channels: {
      stable: publicChannelState(state.channels?.stable),
      dev: publicChannelState(state.channels?.dev),
    },
    lastRun: publicRunState(state.lastRun),
  };
}

async function statusPayload() {
  const internalState = await readState(config.stateFile);
  return {
    ok: true,
    service: "amap-esp32-ota-update-server",
    autoSyncEnabled,
    syncing,
    state: publicState(internalState),
    lastSync: publicLastSync(lastSync),
  };
}

function runSync(reason = "timer") {
  if (!autoSyncEnabled && reason !== "manual-http") {
    console.log("[ota-sync] auto sync disabled");
    return false;
  }
  if (syncing) {
    console.log(`[ota-sync] skip ${reason}, sync already running`);
    return false;
  }
  if (!fs.existsSync(syncScriptPath)) {
    console.log(`[ota-sync] sync script not found: ${syncScriptPath}`);
    return false;
  }
  syncing = true;
  const startedAt = new Date().toISOString();
  console.log(`[ota-sync] start ${reason}`);
  const child = spawn(process.execPath, [syncScriptPath], {
    cwd: baseDir,
    env: process.env,
    stdio: ["ignore", "pipe", "pipe"],
  });
  child.stdout.on("data", (chunk) => process.stdout.write(chunk));
  child.stderr.on("data", (chunk) => process.stderr.write(chunk));
  child.on("close", (code) => {
    syncing = false;
    lastSync = {
      reason,
      code,
      startedAt,
      finishedAt: new Date().toISOString(),
    };
    console.log(`[ota-sync] finish ${reason}, exit=${code}`);
  });
  child.on("error", (error) => {
    syncing = false;
    lastSync = {
      reason,
      code: -1,
      error: error.message,
      startedAt,
      finishedAt: new Date().toISOString(),
    };
    console.error(`[ota-sync] failed ${reason}: ${error.stack || error.message}`);
  });
  return true;
}

function contentTypeFor(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  const types = {
    ".html": "text/html; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".txt": "text/plain; charset=utf-8",
    ".bin": "application/octet-stream",
    ".md": "text/markdown; charset=utf-8",
  };
  return types[ext] || "application/octet-stream";
}

const server = http.createServer(async (req, res) => {
  try {
    const url = new URL(req.url, `http://${req.headers.host}`);

    if (req.method === "GET" && url.pathname === "/health") {
      const payload = await statusPayload();
      sendJson(res, 200, {
        ok: payload.ok,
        service: payload.service,
        syncing: payload.syncing,
        state: {
          generatedAt: payload.state.generatedAt,
          lastRun: payload.state.lastRun,
        },
      });
      return;
    }
    if ((req.method === "GET" || req.method === "POST") && url.pathname === "/sync") {
      let accepted = runSync("manual-http");
      if (req.method === "POST") {
        const body = await readRequestJson(req).catch(() => ({}));
        if (Array.isArray(body.channels) && body.channels.length > 0) {
          console.log(`[ota-sync] manual request channels=${body.channels.join(",")}`);
        }
      }
      sendJson(res, accepted ? 202 : 200, { ok: true, syncing: syncing || accepted });
      return;
    }
    if (req.method === "GET" && url.pathname === "/api/status") {
      sendJson(res, 200, await statusPayload());
      return;
    }
    if (req.method === "GET" && url.pathname === "/api/channels") {
      const state = await readState(config.stateFile);
      sendJson(res, 200, {
        ok: true,
        channels: {
          stable: publicChannelState(state.channels?.stable),
          dev: publicChannelState(state.channels?.dev),
        },
      });
      return;
    }

    const manifestMatch = req.method === "GET"
      ? url.pathname.match(/^\/api\/manifest\/(stable|dev)$/)
      : null;
    if (manifestMatch) {
      sendJson(res, 200, { ok: true, manifest: await manifestForChannel(manifestMatch[1]) });
      return;
    }

    if (req.method === "GET" && url.pathname === "/") {
      sendFile(res, path.join(publicDir, "index.html"), "text/html; charset=utf-8");
      return;
    }

    const staticPath = path.resolve(publicDir, `.${decodeURIComponent(url.pathname)}`);
    if (
      req.method === "GET" &&
      staticPath.startsWith(path.resolve(publicDir) + path.sep) &&
      fs.existsSync(staticPath) &&
      fs.statSync(staticPath).isFile()
    ) {
      sendFile(res, staticPath, contentTypeFor(staticPath));
      return;
    }

    sendText(res, 404, "not found");
  } catch (error) {
    sendJson(res, 500, { ok: false, error: formatError(error) });
  }
});

await ensurePublicFiles();
server.listen(port, host, () => {
  console.log(`AMap OTA update server listening on http://${host}:${port}`);
  console.log(`Health: http://${host}:${port}/health`);
  console.log(`Status: http://${host}:${port}/api/status`);
  console.log(`Manual sync: http://${host}:${port}/sync`);
  if (autoSyncEnabled) {
    console.log(`Auto sync enabled, interval=${syncIntervalMs}ms`);
    runSync("startup");
    setInterval(() => runSync("timer"), syncIntervalMs).unref();
  } else {
    console.log("Auto sync disabled by AUTO_SYNC=0");
  }
});
