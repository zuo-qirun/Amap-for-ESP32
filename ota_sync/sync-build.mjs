#!/usr/bin/env node

import { fileURLToPath } from "node:url";
import path from "node:path";
import { loadEnv } from "./load-env.mjs";
import { buildConfig, formatError, syncAllChannels, usageText } from "./sync-core.mjs";

async function main() {
  const baseDir = path.dirname(fileURLToPath(import.meta.url));
  loadEnv(baseDir);
  const config = buildConfig(process.argv.slice(2), baseDir);
  if (config.help) {
    console.log(usageText());
    return;
  }
  const state = await syncAllChannels(config, console);
  if ((state.lastRun?.failed || 0) > 0 || (state.lastRun?.synced || 0) === 0) {
    process.exitCode = 1;
  }
}

main().catch((error) => {
  console.error(`[fatal] ${formatError(error)}`);
  process.exitCode = 1;
});
