# Changelog

All notable changes to this project are documented here. Format loosely
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## Unreleased

### Added

- GitHub Actions workflow ([`.github/workflows/release-firmware.yml`](.github/workflows/release-firmware.yml)) that builds `p4_cockpit` and merges bootloader + partition table + app + the esp-sr WakeNet model into a single flashable binary (`sensesp-p4-cockpit-merged.bin`) on every published release, attached as a release asset. Also runs on pull requests and pushes to `master` as a build gate, and manually via `workflow_dispatch` (which takes an optional `libs-ref` to pin the sister libs for a reproducible rebuild).
- `scripts/merge_firmware.py` — merges the build's flash images via `esptool merge_bin`, using ESP-IDF's own `flasher_args.json` for offsets. `pio run` does not run esp-sr's `movemodel.py`, so the script falls back to the prepacked `srmodels.bin` shipped in the framework package; `--require-model` makes a missing model image a hard error instead of silently producing a binary with a dead on-device wake word.
- README: "Flashing without installing PlatformIO (browser-based)" section documenting the [ESP Tool](https://www.espboards.dev/tools/program/) web-serial flashing flow using the merged binary.
