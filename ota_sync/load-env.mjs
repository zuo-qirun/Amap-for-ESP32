import fs from "node:fs";
import path from "node:path";

function stripQuotes(value) {
  if (
    (value.startsWith('"') && value.endsWith('"')) ||
    (value.startsWith("'") && value.endsWith("'"))
  ) {
    return value.slice(1, -1);
  }
  return value;
}

export function loadEnv(baseDir, fileName = ".env") {
  const envPath = path.join(baseDir, fileName);
  if (!fs.existsSync(envPath)) {
    return envPath;
  }

  const lines = fs.readFileSync(envPath, "utf8").split(/\r?\n/);
  for (const rawLine of lines) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#")) {
      continue;
    }
    const separator = line.indexOf("=");
    if (separator <= 0) {
      continue;
    }
    const key = line.slice(0, separator).trim();
    const value = stripQuotes(line.slice(separator + 1).trim());
    if (!(key in process.env)) {
      process.env[key] = value;
    }
  }

  return envPath;
}
