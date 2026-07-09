#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import shutil
from datetime import datetime, timezone
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description="Package ESP32 OTA artifacts.")
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--channel", required=True, choices=["dev", "stable"])
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-number", required=True, type=int)
    parser.add_argument("--git-branch", required=True)
    parser.add_argument("--git-commit", required=True)
    parser.add_argument("--firmware-url", default="firmware.bin")
    parser.add_argument("--min-supported-version", default="")
    parser.add_argument("--release-notes", default="")
    args = parser.parse_args()

    if not args.firmware.exists():
        raise SystemExit(f"firmware not found: {args.firmware}")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    firmware_out = args.out_dir / "firmware.bin"
    shutil.copy2(args.firmware, firmware_out)

    digest = sha256_file(firmware_out)
    size = firmware_out.stat().st_size
    (args.out_dir / "firmware.sha256").write_text(
        f"{digest}  firmware.bin\n", encoding="utf-8"
    )

    manifest = {
        "channel": args.channel,
        "version": args.version,
        "build_number": args.build_number,
        "git_branch": args.git_branch,
        "git_commit": args.git_commit,
        "build_time": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "firmware_url": args.firmware_url,
        "sha256": digest,
        "size": size,
        "release_notes": args.release_notes,
    }
    if args.min_supported_version:
        manifest["min_supported_version"] = args.min_supported_version

    (args.out_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
