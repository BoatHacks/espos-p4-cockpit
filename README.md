# sensesp-p4-cockpit

ESP32-P4 firmware for the [Waveshare ESP32-P4-WIFI6-Touch-LCD-7B](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B) — a 1024×600 capacitive-touch helm display that doubles as an NMEA 2000 ↔ SignalK gateway.

The UI is **runtime-loadable**: instead of rebuilding firmware per layout change, the device boots a JSON layout, renders it with LVGL widgets, and binds each widget to a SignalK path over WebSocket. Push a new layout via HTTP from [signalk-hmi-designer](https://github.com/dirkwa/signalk-hmi-designer) and the screen swaps atomically — no flash, no reboot.

## Capabilities

- Native LVGL UI on the 7B's MIPI-DSI panel, capacitive touch.
- **JSON Layout Player (JLP)** — widgets driven by a JSON schema, hot-swap via `POST /layout`.
- Built-in widget kinds: `label`, `value`, `toggle`, `arc`, `bar`, `bargroup`, `button`, `notifications`.
- Two-way SignalK binding: widgets observe paths, taps emit SK PUTs (bool, int, float, string, notification ACK).
- **Zone-color tinting** from SK path metadata (nominal / alert / warn / alarm / emergency), maritime-helm palette.
- **Notifications subsystem** — subscribes to `notifications.*`, surfaces a full-screen alert overlay when any pending notification meets the configured `min_state`, ACK button PUTs the cleared state back as a delta.
- **Audible alert chime** — the panel speaker (ES8311 codec + NS4150B amp) sounds a severity-shaped tone when a notification newly escalates past `min_state` (single beep for warn, urgent double for alarm, fast triple for emergency). Silent on ack/clear.
- **Idle backlight dimmer** — configurable timeout + dim-to brightness per layout. Touch, an incoming notification, or a fresh push all wake the panel.
- **Per-widget colors** — optional `bg_color` / `fg_color` overrides (zone state still wins for alarm cases).
- **Configurable font size** on `label` / `value` widgets via `display.font_size`.
- Always-on status overlay (WiFi, SK WS, N2K rx age, heap, uptime) survives every layout swap.
- **NMEA 2000 gateway** — TWAI receiver/transmitter plus a candump-style TCP server (port 2599) so the bus is reachable from a laptop.
- **Persistence** — successful pushes are saved to LittleFS so the layout survives a power cycle, with a compiled-in default fallback.
- **Boot-time fetch** from SK `applicationData` so a fresh device picks up the fleet's last-known layout.
- mDNS-announced as `_signalk-player._tcp` so designers can discover it.
- HTTP OTA on port 8080.

## Boot priority chain

The device never blanks. At boot the JLP applies layouts in this order, and any later push from `POST /layout` (the design loop) takes precedence at runtime:

1. **LittleFS** — `/lfs/layout.json` from the last successful push.
2. **Compiled-in default** — minimal "no layout pushed yet" fallback in `default_layout.h`.
3. **SignalK `applicationData`** — async GET, applied if it differs from what's already on screen.

If a pushed or fetched layout fails parse/validate/build, the previous layout stays live and the request returns an error — the screen never goes dark.

## HTTP endpoints

| Port | Method | Endpoint        | Purpose                                       |
|------|--------|-----------------|-----------------------------------------------|
| 8081 | GET    | `/hello`        | Capability descriptor (schema, widgets, display, active layout, screenshot formats) |
| 8081 | POST   | `/layout`       | Apply a layout JSON (≤64 KB), atomic swap + LittleFS persist |
| 8081 | GET    | `/screenshot`   | Framebuffer dump. Default: software-encoded JPEG. `?fmt=bmp` for the legacy RGB565 BMP. |
| 8081 | GET    | `/healthz`      | Liveness probe                                |
| 8080 | POST   | (firmware OTA)  | Upload a new firmware bin                     |
| 2599 | TCP    | (candump)       | Stream raw N2K frames in candump format       |
| 2323 | TCP    | (remote log)    | Live ESP-IDF log stream                       |

The full contract — request/response shapes, schema, widget fields, error codes — is in [JLP-PROTOCOL.md](JLP-PROTOCOL.md).

## Architecture

```
   ┌────────────────────────────────────────────────────────┐
   │ HMI Designer (browser) ──POST /layout──> JLP HTTP API  │
   │                         <─── /hello /screenshot ───    │
   └────────────────────────────────────────────────────────┘
                                  │
                                  ▼
        ┌───────────────────────────────────────────────┐
        │ JLP LayoutManager (event_loop task, LVGL-only)│
        │  parse → validate → stage (hidden) → swap     │
        │                                  → LittleFS   │
        │                  → idle_dimmer.configure(...)  │
        │                  → alert_overlay.configure(...)│
        └───────────────────────────────────────────────┘
              │                                  ▲
              │ creates                          │ subject updates +
              ▼                                  │ zone meta + notifs
   ┌───────────────────────┐         ┌──────────────────────────┐
   │ LVGL widget tree      │         │ Subject + Zone +         │
   │ label / value /       │         │ Notifications registries │
   │ toggle / arc / bar /  │         └──────────────────────────┘
   │ bargroup / button /   │                  ▲
   │ notifications         │                  │
   └───────────────────────┘                  │ on_meta / on_value
                                              │ + REST /meta fetch
                                       ┌──────────────┐
                                       │ SensESP WS   │──> SK server
                                       │ (sendMeta=all)│
                                       └──────────────┘

   ┌───────────────────────────────┐
   │ N2K gateway (TWAI rx/tx)      │── candump TCP server (port 2599)
   └───────────────────────────────┘
```

LVGL is single-threaded. The HTTP task parses + validates a posted layout, then trampolines the build/swap onto the `event_loop` task via `event_loop()->onDelay(0, ...)` so only one task ever touches LVGL.

The **status overlay** sits above the layout tabview in z-order and is created once at boot; it survives every layout swap. The **alert overlay** sits above the status overlay and pops up whenever an incoming notification clears the configured `min_state` threshold.

## Idle backlight dimmer

Layouts can opt the device into a power-save mode via the top-level `display` block:

```jsonc
{
  "schema": 1,
  "name": "Helm",
  "display": {
    "idle_timeout_sec": 300,   // 0 / omitted = always on
    "idle_dim_pct": 80         // 0-100, default 80
  },
  "screens": [...]
}
```

Hardware note: the Waveshare 7B's GT911 touch controller's sensitivity drops sharply as soon as the LCD backlight dims, because the LCD's continued sync signals dominate the touch sensing layer. **Tap-wake is only reliable at brightnesses close to full.** The 80 % default reads as clearly "asleep" while keeping tap-wake working. Lower dim levels save more power but only notifications and fresh layout pushes will then wake the panel.

Wake sources:
- any touch (always, but only effective at high `idle_dim_pct`)
- any incoming SignalK notification at or above the configured `min_state`
- a fresh `POST /layout`

## Build & flash

PlatformIO with the pioarduino ESP32-P4 build:

```bash
pio run -e p4_cockpit                  # build
pio run -e p4_cockpit -t upload        # build + flash via /dev/ttyACM0
pio device monitor                     # local serial
nc <device-ip> 2323                    # remote log stream
```

Target: `esp32-p4`, 16 MB flash, PSRAM enabled, LittleFS partition. See [platformio.ini](platformio.ini).

## Which wake word you get depends on the env you build

The hands-free wake word is **chosen at build time**, and the two envs listen for different words. Building the wrong one is silent — the panel comes up healthy, streams audio, and simply never wakes.

| Env | Wake runs | Word | Needs |
|---|---|---|---|
| `p4_cockpit` (default) | on-device (esp-sr WakeNet) | **"Hi ESP"** | the `model` partition flashed |
| `p4_cockpit_netwake` | [signalk-openwakeword](https://github.com/dirkwa/signalk-openwakeword) over the network | whatever the server loads, e.g. **"hey moin"** | that plugin running and reachable |

**Each release ships both variants** — `p4_cockpit-merged.bin` (on-device, *"Hi ESP"*) and `p4_cockpit_netwake-merged.bin` (network wake). Flash whichever matches the table above; for a wake word other than what the server already serves, or to point the netwake build at a specific host, build it yourself:

```bash
pio run -e p4_cockpit_netwake -t upload
```

Its host/word are compile-time flags in [platformio.ini](platformio.ini) (`JLP_NETWORK_WAKE_HOST`, `JLP_NETWORK_WAKE_WORD`); point them at your SignalK server and the model name the plugin serves. The name must match a model the plugin actually loads — an empty or unknown detect list makes openWakeWord silently fall back to its own default model.

`GET /hello` reports which mode is live under `wake`: `on_device` tells the two apart, and on the network path `chunks` climbing proves the mic is reaching the detector (both stay `0` on-device by design).

The firmware depends on released [SensESP](https://github.com/SignalK/SensESP) `>= 3.5.0` (pinned in [platformio.ini](platformio.ini)). The WS meta stream (`sendMeta=all` + `SKMetadataListener`) — how widgets learn zone definitions for color tinting — and the `SKPrefixListener` the notifications registry uses are all in that release.

## Flashing without installing PlatformIO (browser-based)

For the very first flash — or recovery if a device won't boot — you don't need PlatformIO or the sister libs checked out on the flashing machine. [ESP Tool](https://www.espboards.dev/tools/program/) flashes over USB directly from the browser via the Web Serial API. This only replaces the one-time step of writing bootloader + partition table + app to a blank board; normal updates after that go over HTTP OTA (port 8080), not through this tool.

1. **Browser**: Chrome, Edge, or Opera (Web Serial API isn't supported in Firefox or Safari).
2. **Get the firmware**: every published [release](https://github.com/BoatHacks/sensesp-p4-cockpit/releases) has `p4_cockpit-merged.bin` (and `p4_cockpit_netwake-merged.bin`) — a single file with the bootloader, partition table, app image, and the esp-sr WakeNet model partition already combined via `esptool merge_bin` (see [`.github/workflows/release-firmware.yml`](.github/workflows/release-firmware.yml)), flashable in one shot at offset `0x0`. `otadata` is deliberately left out: an erased `otadata` reads as all-`0xFF`, which the bootloader treats as "boot the first OTA slot" — exactly what a fresh flash wants. You can also build it yourself: `pio run -e p4_cockpit && python scripts/merge_firmware.py --build-dir .pio/build/p4_cockpit --chip esp32p4 --require-model --out p4_cockpit-merged.bin`.
3. **Connect**: plug the board's USB-UART port into your computer, open the ESP Tool page, and click **Connect** to pick the serial port. If the board doesn't auto-reset into download mode, put it there manually: hold **BOOT**, tap **RESET**, then release **BOOT**.
4. Switch to the **Flash firmware** tab, **Add File**, pick the merged `.bin`, and set its offset to `0x0`.
5. **Flash settings**: mode `dio`, freq `80m`, size `16MB` (see [`p4_16mb.csv`](p4_16mb.csv) / `board_upload.flash_size` in [platformio.ini](platformio.ini)).
6. Click **Program** and wait for it to finish, then reset the board. Open a serial monitor (ESP Tool's built-in one, or `pio device monitor`) to confirm it boots and joins Wi-Fi.

## Push your first layout

Once the device prints `mDNS service registered: p4-cockpit._signalk-player._tcp` to the remote log:

```bash
IP=<device-ip>
curl -sf http://$IP:8081/hello | jq .              # see what the device supports
curl -sf -X POST -H "Content-Type: application/json" \
  --data-binary @my-layout.json \
  http://$IP:8081/layout                            # swap atomically
```

Or open the SK admin UI, launch **HMI Designer**, point it at `http://<device-ip>:8081`, and drag.

## Related projects

- [signalk-hmi-designer](https://github.com/dirkwa/signalk-hmi-designer) — the SignalK webapp that designs and pushes layouts (drag-and-drop canvas, pixel-perfect WASM preview, live mirror mode).
- [sensesp-p4-cockpit-wasm](https://github.com/dirkwa/sensesp-p4-cockpit-wasm) — the same `widget_factory.cpp` compiled to WebAssembly so the designer can render layouts pixel-identically to the device without one being connected.
- **sensesp-cockpit-display** — shared HAL (display, touch, idle backlight) + OTA + remote-log library for the Waveshare 7B (linked as a local symlink in `platformio.ini`).
- **sensesp-n2k-gateway** — the N2K-over-TWAI + candump TCP gateway library (also a local symlink).
