# Changelog

All notable changes to this project are documented here. Format loosely
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## Unreleased

### Added

- GitHub Actions workflow ([`.github/workflows/release-firmware.yml`](.github/workflows/release-firmware.yml)) that builds `p4_cockpit` and merges bootloader + partition table + `otadata` + app into a single flashable binary (`sensesp-p4-cockpit-merged.bin`) on every published release, attached as a release asset. Also runnable manually via `workflow_dispatch`.
- `scripts/merge_firmware.py` — merges the build's flash images via `esptool merge_bin`, using ESP-IDF's own `flasher_args.json` for offsets.
- README: "Flashing without installing PlatformIO (browser-based)" section documenting the [ESP Tool](https://www.espboards.dev/tools/program/) web-serial flashing flow using the merged binary.
