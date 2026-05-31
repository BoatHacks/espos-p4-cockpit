# sensesp-p4-cockpit

ESP32-P4 firmware for the [Waveshare ESP32-P4-WIFI6-Touch-LCD-7B](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B) — a 1024×600 capacitive-touch helm display that doubles as an NMEA 2000 ↔ SignalK gateway.

The UI is **runtime-loadable**: instead of rebuilding firmware per layout change, the device boots a JSON layout, renders it with LVGL widgets, and binds each widget to a SignalK path over WebSocket. Push a new layout via HTTP from [signalk-hmi-designer](https://github.com/dirkwa/signalk-hmi-designer) and the screen swaps atomically — no flash, no reboot.

## Capabilities

- Native LVGL UI on the 7B's MIPI-DSI panel, capacitive touch.
- **JSON Layout Player (JLP)** — widgets driven by a JSON schema, hot-swap via `POST /layout`.
- Built-in widget kinds: `label`, `toggle`, `arc`, `bar` (`button` planned).
- Two-way SignalK binding: widgets observe paths, taps emit SK PUTs.
- Zone-color tinting from SK path metadata (nominal / alert / warn / alarm / emergency).
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
| 8081 | GET    | `/hello`        | Capability descriptor (schema, widgets, display, active layout) |
| 8081 | POST   | `/layout`       | Apply a layout JSON (≤64 KB), atomic swap + LittleFS persist |
| 8081 | GET    | `/screenshot`   | RGB565 BMP framebuffer dump (for designer overlay) |
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
        └───────────────────────────────────────────────┘
              │                                  ▲
              │ creates                          │ subject updates
              ▼                                  │
   ┌───────────────────────┐         ┌─────────────────────┐
   │ LVGL widget tree      │         │ SubjectRegistry +   │
   │ (label/toggle/arc/bar)│         │ ZoneRegistry        │
   └───────────────────────┘         └─────────────────────┘
                                              ▲
                                              │ deltas + meta
                                              │
                                       ┌──────────────┐
                                       │ SensESP WS   │──> SK server
                                       │ (sendMeta=all)│
                                       └──────────────┘

   ┌───────────────────────────────┐
   │ N2K gateway (TWAI rx/tx)      │── candump TCP server (port 2599)
   └───────────────────────────────┘
```

LVGL is single-threaded. The HTTP task parses + validates a posted layout, then trampolines the build/swap onto the `event_loop` task via `event_loop()->onDelay(0, ...)` so only one task ever touches LVGL.

The status overlay sits **above** the layout tabview in z-order and is created once at boot; it survives every layout swap and cannot be specified or hidden via JSON (a user-shipped blank layout still shows WiFi/SK/N2K state).

## Build & flash

PlatformIO with the pioarduino ESP32-P4 build:

```bash
pio run -e p4_cockpit                  # build
pio run -e p4_cockpit -t upload        # build + flash via /dev/ttyACM0
pio device monitor                     # local serial
nc <device-ip> 2323                    # remote log stream
```

Target: `esp32-p4`, 16 MB flash, PSRAM enabled, LittleFS partition. See [platformio.ini](platformio.ini).

The firmware currently depends on a local checkout of [SensESP](https://github.com/SignalK/SensESP) with the `sendMeta=all` WS feature ([PR #965](https://github.com/SignalK/SensESP/pull/965)) cherry-picked — the WS meta stream is how widgets learn zone definitions for color tinting. Once PR #965 merges, the symlink in `platformio.ini` can drop and the upstream library can be pinned.

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

- [signalk-hmi-designer](https://github.com/dirkwa/signalk-hmi-designer) — the SignalK webapp that designs and pushes layouts.
- **sensesp-cockpit-display** — shared HAL + OTA + remote-log library for the Waveshare 7B (linked as a local symlink in `platformio.ini`).
- **sensesp-n2k-gateway** — the N2K-over-TWAI + candump TCP gateway library (also a local symlink).
