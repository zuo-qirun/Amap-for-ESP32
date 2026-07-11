import crypto from "node:crypto";
import fs from "node:fs/promises";
import { createReadStream, createWriteStream } from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawn } from "node:child_process";
import { Readable } from "node:stream";
import { pipeline } from "node:stream/promises";
import unzipper from "unzipper";

export const REQUIRED_FILES = ["firmware.bin", "firmware.sha256", "manifest.json"];
const OPTIONAL_FILES = ["CHANGELOG.md"];
const PUBLISH_ORDER = ["firmware.bin", "firmware.sha256", "CHANGELOG.md", "manifest.json"];

export class HttpError extends Error {
  constructor(message, status, body = "") {
    super(message);
    this.name = "HttpError";
    this.status = status;
    this.body = body;
  }
}

export class MissingChannelError extends Error {
  constructor(message) {
    super(message);
    this.name = "MissingChannelError";
  }
}

function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i += 1) {
    const token = argv[i];
    if (!token.startsWith("--")) {
      continue;
    }
    const key = token.slice(2);
    const next = argv[i + 1];
    if (!next || next.startsWith("--")) {
      args[key] = "true";
      continue;
    }
    args[key] = next;
    i += 1;
  }
  return args;
}

export function usageText() {
  return `Usage:
  node sync-build.mjs [options]

Options:
  --repo owner/repo           GitHub repo, default: zuo-qirun/Amap-for-ESP32
  --channels stable,dev       Comma-separated channel list, default: stable,dev
  --source release|artifact   Sync source, default: release
  --web-root /path/to/ota     OTA root directory, default: ./public/ota
  --github-token TOKEN        Required for artifact sync, optional for release sync
  --strict-missing-channel    Fail when a requested channel is not published yet
  --state-file /path.json     State JSON path, default: ./state/sync-state.json
  --help                      Show this message
`;
}

function resolveConfigPath(baseDir, targetPath) {
  if (!targetPath) {
    return path.resolve(baseDir);
  }
  if (path.isAbsolute(targetPath)) {
    return targetPath;
  }
  return path.resolve(baseDir, targetPath);
}

export function buildConfig(argv = process.argv.slice(2), baseDir = process.cwd()) {
  const args = parseArgs(argv);
  if (args.help === "true") {
    return { help: true };
  }

  const repo = args.repo || process.env.GITHUB_REPO || "zuo-qirun/Amap-for-ESP32";
  const source = args.source || process.env.OTA_SYNC_SOURCE || "release";
  const webRoot = args["web-root"] || process.env.OTA_WEB_ROOT || path.join(baseDir, "public", "ota");
  const githubToken = args["github-token"] || process.env.GITHUB_TOKEN || "";
  const strictMissing =
    args["strict-missing-channel"] === "true" ||
    process.env.OTA_SYNC_STRICT_MISSING === "1";
  const channels = [
    ...new Set(
      (args.channels || process.env.OTA_SYNC_CHANNELS || "stable,dev")
        .split(",")
        .map((item) => item.trim())
        .filter(Boolean)
    ),
  ];
  const stateFile =
    args["state-file"] || process.env.OTA_SYNC_STATE_FILE || path.join(baseDir, "state", "sync-state.json");

  if (!repo.includes("/")) {
    throw new Error(`invalid repo: ${repo}`);
  }
  if (!["release", "artifact"].includes(source)) {
    throw new Error(`invalid source: ${source}`);
  }
  if (source === "artifact" && !githubToken) {
    throw new Error("artifact sync requires --github-token or GITHUB_TOKEN");
  }
  for (const channel of channels) {
    if (!["stable", "dev"].includes(channel)) {
      throw new Error(`invalid channel: ${channel}`);
    }
  }

  return {
    repo,
    source,
    webRoot: resolveConfigPath(baseDir, webRoot),
    githubToken,
    strictMissing,
    channels,
    stateFile: resolveConfigPath(baseDir, stateFile),
  };
}

function githubHeaders(token, accept = "application/vnd.github+json") {
  const headers = {
    Accept: accept,
    "User-Agent": "amap-esp32-ota-update-server",
  };
  if (token) {
    headers.Authorization = `Bearer ${token}`;
  }
  return headers;
}

async function fetchJson(url, token) {
  try {
    const response = await fetch(url, { headers: githubHeaders(token) });
    if (!response.ok) {
      throw new HttpError(
        `GitHub HTTP ${response.status} for ${url}`,
        response.status,
        await response.text()
      );
    }
    return response.json();
  } catch (error) {
    if (!shouldUseCurlFallback(error)) {
      throw error;
    }
    const body = await curlGet(url, token, "application/vnd.github+json");
    return JSON.parse(body);
  }
}

async function downloadFile(url, outFile, token) {
  try {
    const response = await fetch(url, {
      headers: githubHeaders(token, "application/octet-stream"),
      redirect: "follow",
    });
    if (!response.ok || !response.body) {
      throw new HttpError(
        `Download HTTP ${response.status} for ${url}`,
        response.status,
        await response.text()
      );
    }
    await fs.mkdir(path.dirname(outFile), { recursive: true });
    await pipeline(Readable.fromWeb(response.body), createWriteStream(outFile));
    return;
  } catch (error) {
    if (!shouldUseCurlFallback(error)) {
      throw error;
    }
    await fs.mkdir(path.dirname(outFile), { recursive: true });
    await curlDownload(url, outFile, token);
  }
}

async function syncRelease(config, channel, tempDir) {
  const releaseUrl = `https://api.github.com/repos/${config.repo}/releases/tags/ota-${channel}-latest`;
  let release;
  try {
    release = await fetchJson(releaseUrl, config.githubToken);
  } catch (error) {
    if (error instanceof HttpError && error.status === 404) {
      throw new MissingChannelError(`release ota-${channel}-latest does not exist yet`);
    }
    throw error;
  }

  const assets = new Map((release.assets || []).map((asset) => [asset.name, asset]));
  const missingAssets = REQUIRED_FILES.filter((name) => !assets.has(name));
  if (missingAssets.length > 0) {
    throw new MissingChannelError(
      `release ota-${channel}-latest missing assets: ${missingAssets.join(", ")}`
    );
  }

  for (const name of REQUIRED_FILES) {
    await downloadFile(
      assets.get(name).browser_download_url,
      path.join(tempDir, name),
      config.githubToken
    );
  }
  for (const name of OPTIONAL_FILES) {
    if (!assets.has(name)) {
      continue;
    }
    await downloadFile(
      assets.get(name).browser_download_url,
      path.join(tempDir, name),
      config.githubToken
    );
  }
}

async function extractZip(zipFile, destDir) {
  await pipeline(createReadStream(zipFile), unzipper.Extract({ path: destDir }));
}

async function syncArtifact(config, channel, tempDir) {
  const artifactsUrl = `https://api.github.com/repos/${config.repo}/actions/artifacts?per_page=100`;
  const artifacts = await fetchJson(artifactsUrl, config.githubToken);
  const prefix = `amap-esp32-${channel}-`;
  const candidates = (artifacts.artifacts || [])
    .filter((artifact) => artifact.name?.startsWith(prefix) && !artifact.expired)
    .sort((left, right) => right.created_at.localeCompare(left.created_at));

  if (candidates.length === 0) {
    throw new MissingChannelError(`no non-expired artifact found for ${channel}`);
  }

  const archivePath = path.join(tempDir, "artifact.zip");
  await downloadFile(candidates[0].archive_download_url, archivePath, config.githubToken);
  await extractZip(archivePath, tempDir);
}

async function walk(dir, result = new Map()) {
  const entries = await fs.readdir(dir, { withFileTypes: true });
  for (const entry of entries) {
    const fullPath = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      await walk(fullPath, result);
      continue;
    }
    if (!result.has(entry.name)) {
      result.set(entry.name, fullPath);
    }
  }
  return result;
}

async function sha256File(filePath) {
  const hash = crypto.createHash("sha256");
  const stream = createReadStream(filePath);
  for await (const chunk of stream) {
    hash.update(chunk);
  }
  return hash.digest("hex");
}

function validateManifest(manifest, channel) {
  const requiredFields = [
    "channel",
    "version",
    "git_branch",
    "git_commit",
    "build_time",
    "firmware_url",
    "sha256",
    "size",
  ];
  for (const field of requiredFields) {
    if (!(field in manifest)) {
      throw new Error(`manifest missing field: ${field}`);
    }
  }
  if (manifest.channel !== channel) {
    throw new Error(`manifest channel mismatch: expected ${channel}, got ${manifest.channel}`);
  }
  if (!/^[a-f0-9]{64}$/i.test(String(manifest.sha256))) {
    throw new Error("manifest sha256 is not a 64-char hex string");
  }
}

async function publish(tempDir, targetDir, channel) {
  const fileMap = await walk(tempDir);
  for (const name of REQUIRED_FILES) {
    if (!fileMap.has(name)) {
      throw new Error(`downloaded payload missing ${name}`);
    }
  }

  const manifestPath = fileMap.get("manifest.json");
  const firmwarePath = fileMap.get("firmware.bin");
  const shaFilePath = fileMap.get("firmware.sha256");
  const manifest = JSON.parse(await fs.readFile(manifestPath, "utf8"));
  validateManifest(manifest, channel);
  if (!("changelog_url" in manifest)) {
    manifest.changelog_url = "CHANGELOG.md";
  }

  const firmwareSha = await sha256File(firmwarePath);
  if (firmwareSha !== String(manifest.sha256).toLowerCase()) {
    throw new Error(`firmware sha256 mismatch for ${channel}`);
  }

  const shaFile = (await fs.readFile(shaFilePath, "utf8")).trim().toLowerCase();
  if (!shaFile.includes(firmwareSha)) {
    throw new Error(`firmware.sha256 does not contain the expected hash for ${channel}`);
  }

  const actualSize = (await fs.stat(firmwarePath)).size;
  if (Number(manifest.size) !== actualSize) {
    throw new Error(
      `firmware size mismatch for ${channel}: manifest=${manifest.size}, actual=${actualSize}`
    );
  }

  await fs.mkdir(targetDir, { recursive: true });

  for (const name of PUBLISH_ORDER) {
    const srcPath = fileMap.get(name);
    const tempTarget = path.join(targetDir, `${name}.tmp-${process.pid}`);
    const finalTarget = path.join(targetDir, name);
    await fs.copyFile(srcPath, tempTarget);
    await fs.rename(tempTarget, finalTarget);
  }

  return {
    channel,
    version: String(manifest.version),
    buildNumber: Number(manifest.build_number || 0),
    gitBranch: String(manifest.git_branch || ""),
    gitCommit: String(manifest.git_commit || ""),
    buildTime: String(manifest.build_time || ""),
    releaseNotes: String(manifest.release_notes || ""),
    changelogUrl: String(manifest.changelog_url || ""),
    manifest,
  };
}

export function formatError(error) {
  if (error instanceof HttpError) {
    return `${error.message}${error.body ? ` :: ${error.body}` : ""}`;
  }
  return error instanceof Error ? error.message : String(error);
}

function shouldUseCurlFallback(error) {
  if (!(error instanceof Error)) {
    return false;
  }
  const text = `${error.message}\n${error.stack || ""}`;
  return (
    text.includes("UNABLE_TO_VERIFY_LEAF_SIGNATURE") ||
    text.includes("unable to verify the first certificate") ||
    text.includes("SELF_SIGNED_CERT") ||
    text.includes("fetch failed")
  );
}

function runCurl(args) {
  return new Promise((resolve, reject) => {
    const child = spawn("curl", args, { stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "";
    let stderr = "";

    child.stdout.on("data", (chunk) => {
      stdout += chunk.toString();
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString();
    });
    child.on("error", reject);
    child.on("close", (code) => {
      if (code === 0) {
        resolve({ stdout, stderr });
        return;
      }
      reject(new Error(`curl exited with code ${code}: ${stderr || stdout}`));
    });
  });
}

async function curlGet(url, token, accept) {
  const args = [
    "-sS",
    "-L",
    "-w",
    "\n%{http_code}",
    "-H",
    "User-Agent: amap-esp32-ota-update-server",
    "-H",
    `Accept: ${accept}`,
  ];
  if (token) {
    args.push("-H", `Authorization: Bearer ${token}`);
  }
  args.push(url);
  const { stdout } = await runCurl(args);
  const splitAt = stdout.lastIndexOf("\n");
  const body = splitAt >= 0 ? stdout.slice(0, splitAt) : stdout;
  const statusText = splitAt >= 0 ? stdout.slice(splitAt + 1).trim() : "";
  const status = Number(statusText || 0);
  if (!Number.isFinite(status) || status < 200 || status >= 300) {
    throw new HttpError(`GitHub HTTP ${status || "unknown"} for ${url}`, status || 0, body);
  }
  return body;
}

async function curlDownload(url, outFile, token) {
  const args = ["-sS", "-L", "-H", "User-Agent: amap-esp32-ota-update-server"];
  if (token) {
    args.push("-H", `Authorization: Bearer ${token}`);
  }
  args.push("-o", outFile, url);
  await runCurl(args);
}

export async function syncChannel(config, channel) {
  const tempDir = await fs.mkdtemp(path.join(os.tmpdir(), `amap-ota-${channel}-`));
  try {
    if (config.source === "release") {
      await syncRelease(config, channel, tempDir);
    } else {
      await syncArtifact(config, channel, tempDir);
    }
    return await publish(tempDir, path.join(config.webRoot, channel), channel);
  } finally {
    await fs.rm(tempDir, { recursive: true, force: true });
  }
}

export async function readState(stateFile) {
  try {
    return JSON.parse(await fs.readFile(stateFile, "utf8"));
  } catch {
    return {
      schemaVersion: 1,
      generatedAt: null,
      repo: "",
      source: "",
      channels: {},
      lastRun: null,
    };
  }
}

export async function writeState(stateFile, state) {
  await fs.mkdir(path.dirname(stateFile), { recursive: true });
  await fs.writeFile(stateFile, JSON.stringify(state, null, 2) + "\n", "utf8");
}

export async function syncAllChannels(config, logger = console) {
  let synced = 0;
  let skipped = 0;
  let failed = 0;
  const startedAt = new Date();
  const channels = {};

  logger.log(
    `[sync] repo=${config.repo} source=${config.source} channels=${config.channels.join(",")} webRoot=${config.webRoot}`
  );

  for (const channel of config.channels) {
    try {
      const result = await syncChannel(config, channel);
      synced += 1;
      channels[channel] = {
        status: "synced",
        syncedAt: new Date().toISOString(),
        version: result.version,
        buildNumber: result.buildNumber,
        gitBranch: result.gitBranch,
        gitCommit: result.gitCommit,
        buildTime: result.buildTime,
        releaseNotes: result.releaseNotes,
        manifest: result.manifest,
        lastError: "",
      };
      logger.log(
        `[${channel}] synced version=${result.version} build=${result.buildNumber} commit=${result.gitCommit}`
      );
    } catch (error) {
      if (error instanceof MissingChannelError && !config.strictMissing) {
        skipped += 1;
        channels[channel] = {
          status: "skipped",
          syncedAt: null,
          version: "",
          buildNumber: 0,
          gitBranch: "",
          gitCommit: "",
          buildTime: "",
          releaseNotes: "",
          manifest: null,
          lastError: error.message,
        };
        logger.warn(`[${channel}] skipped: ${error.message}`);
        continue;
      }
      failed += 1;
      channels[channel] = {
        status: "failed",
        syncedAt: null,
        version: "",
        buildNumber: 0,
        gitBranch: "",
        gitCommit: "",
        buildTime: "",
        releaseNotes: "",
        manifest: null,
        lastError: formatError(error),
      };
      logger.error(`[${channel}] failed: ${formatError(error)}`);
    }
  }

  const endedAt = new Date();
  const state = {
    schemaVersion: 1,
    generatedAt: endedAt.toISOString(),
    repo: config.repo,
    source: config.source,
    webRoot: config.webRoot,
    channels,
    lastRun: {
      startedAt: startedAt.toISOString(),
      finishedAt: endedAt.toISOString(),
      synced,
      skipped,
      failed,
    },
  };
  await writeState(config.stateFile, state);
  logger.log(`[summary] synced=${synced} skipped=${skipped} failed=${failed}`);
  return state;
}
