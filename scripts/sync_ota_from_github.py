#!/usr/bin/env python3
"""
Sync OTA artifacts from GitHub Actions artifacts or rolling Releases to a local
web root such as /var/www/html/ota.

Examples:
  python scripts/sync_ota_from_github.py --repo zuo-qirun/Amap-for-ESP32 --channel dev --source release --web-root /var/www/html/ota
  python scripts/sync_ota_from_github.py --repo zuo-qirun/Amap-for-ESP32 --channel stable --source artifact --web-root /var/www/html/ota --github-token "$GITHUB_TOKEN"
"""
import argparse
import json
import os
import shutil
import tempfile
import zipfile
from pathlib import Path
from typing import Optional
from urllib.error import HTTPError
from urllib.request import Request, urlopen


REQUIRED = {"firmware.bin", "firmware.sha256", "manifest.json"}


def github_get(url: str, token: Optional[str] = None) -> bytes:
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "amap-esp32-ota-sync",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = Request(url, headers=headers)
    with urlopen(request, timeout=60) as response:
        return response.read()


def download_file(url: str, out: Path, token: Optional[str] = None) -> None:
    data = github_get(url, token)
    out.write_bytes(data)


def sync_release(repo: str, channel: str, temp_dir: Path, token: Optional[str]) -> None:
    release_url = f"https://api.github.com/repos/{repo}/releases/tags/ota-{channel}-latest"
    release = json.loads(github_get(release_url, token).decode("utf-8"))
    assets = {asset["name"]: asset for asset in release.get("assets", [])}
    missing = REQUIRED - set(assets)
    if missing:
        raise SystemExit(f"release missing assets: {', '.join(sorted(missing))}")
    for name in REQUIRED:
        download_file(assets[name]["browser_download_url"], temp_dir / name, token)


def sync_artifact(repo: str, channel: str, temp_dir: Path, token: Optional[str]) -> None:
    artifacts_url = f"https://api.github.com/repos/{repo}/actions/artifacts?per_page=100"
    artifacts = json.loads(github_get(artifacts_url, token).decode("utf-8"))
    prefix = f"amap-esp32-{channel}-"
    candidates = [
        artifact for artifact in artifacts.get("artifacts", [])
        if artifact.get("name", "").startswith(prefix) and not artifact.get("expired")
    ]
    if not candidates:
        raise SystemExit(f"no non-expired artifact found for channel {channel}")
    candidates.sort(key=lambda item: item.get("created_at", ""), reverse=True)
    archive_url = candidates[0]["archive_download_url"]

    zip_path = temp_dir / "artifact.zip"
    download_file(archive_url, zip_path, token)
    with zipfile.ZipFile(zip_path) as archive:
        archive.extractall(temp_dir)


def publish(temp_dir: Path, target_dir: Path) -> None:
    missing = [name for name in REQUIRED if not (temp_dir / name).exists()]
    if missing:
        raise SystemExit(f"downloaded artifact missing: {', '.join(missing)}")

    target_dir.mkdir(parents=True, exist_ok=True)
    staging = target_dir.parent / f".{target_dir.name}.staging"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    for name in REQUIRED:
        shutil.copy2(temp_dir / name, staging / name)

    for name in REQUIRED:
        shutil.move(str(staging / name), str(target_dir / name))
    shutil.rmtree(staging)


def main() -> None:
    parser = argparse.ArgumentParser(description="Sync AMap ESP32 OTA artifacts.")
    parser.add_argument("--repo", required=True, help="owner/repo, e.g. zuo-qirun/Amap-for-ESP32")
    parser.add_argument("--channel", required=True, choices=["dev", "stable"])
    parser.add_argument("--source", required=True, choices=["release", "artifact"])
    parser.add_argument("--web-root", default="/var/www/html/ota")
    parser.add_argument("--github-token", default=os.environ.get("GITHUB_TOKEN"))
    args = parser.parse_args()

    if args.source == "artifact" and not args.github_token:
        raise SystemExit("artifact sync requires --github-token or GITHUB_TOKEN")

    with tempfile.TemporaryDirectory() as temp:
        temp_dir = Path(temp)
        try:
            if args.source == "release":
                sync_release(args.repo, args.channel, temp_dir, args.github_token)
            else:
                sync_artifact(args.repo, args.channel, temp_dir, args.github_token)
        except HTTPError as exc:
            raise SystemExit(f"GitHub HTTP {exc.code}: {exc.reason}") from exc

        publish(temp_dir, Path(args.web_root) / args.channel)


if __name__ == "__main__":
    main()
