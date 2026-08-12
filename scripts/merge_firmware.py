#!/usr/bin/env python3
"""Merge a PlatformIO/ESP-IDF build's flash images into one binary.

Reads the offset/file map ESP-IDF's build already worked out
(flasher_args.json in the build dir) and hands it to esptool's
merge_bin, so the offsets here can never drift from what the
partition table + bootloader actually need.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path,
                         help="e.g. .pio/build/p4_cockpit")
    parser.add_argument("--chip", required=True, help="e.g. esp32p4")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    flasher_args_path = args.build_dir / "flasher_args.json"
    if not flasher_args_path.is_file():
        print(f"error: {flasher_args_path} not found — build the firmware first",
              file=sys.stderr)
        return 1

    flasher_args = json.loads(flasher_args_path.read_text())
    flash_settings = flasher_args["flash_settings"]
    flash_files = flasher_args["flash_files"]

    cmd = [
        "esptool.py", "--chip", args.chip, "merge_bin",
        "-o", str(args.out),
        "--flash_mode", flash_settings["flash_mode"],
        "--flash_freq", flash_settings["flash_freq"],
        "--flash_size", flash_settings["flash_size"],
    ]
    for offset, rel_path in sorted(flash_files.items(), key=lambda kv: int(kv[0], 16)):
        cmd += [offset, str(args.build_dir / rel_path)]

    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
