#!/usr/bin/env python3
import argparse
import configparser
from pathlib import Path


def escaped_cpp_string(value: str) -> str:
    return '\\"' + value.replace("\\", "\\\\").replace('"', '\\"') + '\\"'


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate a PlatformIO config for CI firmware metadata.")
    parser.add_argument("--base", default="platformio.ini", type=Path)
    parser.add_argument("--out", default="platformio-ci.ini", type=Path)
    parser.add_argument("--env", default="esp32-s3-devkitm-1")
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-number", required=True, type=int)
    parser.add_argument("--channel", required=True, choices=["dev", "stable"])
    args = parser.parse_args()

    section = f"env:{args.env}"
    config = configparser.ConfigParser()
    config.optionxform = str
    with args.base.open("r", encoding="utf-8") as handle:
        config.read_file(handle)

    if section not in config:
        raise SystemExit(f"missing PlatformIO section: [{section}]")

    config[section]["build_flags"] = "\n".join(
        [
            "-D ARDUINO_USB_MODE=1",
            "-D ARDUINO_USB_CDC_ON_BOOT=1",
            f"-D AMAP_FIRMWARE_VERSION={escaped_cpp_string(args.version)}",
            f"-D AMAP_FIRMWARE_BUILD={args.build_number}",
            f"-D AMAP_FIRMWARE_CHANNEL={escaped_cpp_string(args.channel)}",
        ]
    )

    with args.out.open("w", encoding="utf-8", newline="\n") as handle:
        config.write(handle)


if __name__ == "__main__":
    main()
