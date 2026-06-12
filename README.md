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

The firmware currently depends on a local checkout of [SensESP](https://github.com/SignalK/SensESP) (branch `local/jlp-bridge`) which carries the `sendMeta=all` WS feature + `on_meta` / `on_value` hooks — the WS meta stream is how widgets learn zone definitions for color tinting. Revert to upstream once the matching PRs merge.

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
