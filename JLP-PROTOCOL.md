# JLP Protocol (schema 1)

The **JSON Layout Player (JLP)** is a runtime widget engine that lets a SignalK-aware display device load and re-render its UI from a JSON document, without a firmware rebuild. This document specifies the wire contract — the HTTP endpoints, the JSON schema, and the device behaviour a client (e.g. [signalk-hmi-designer](https://github.com/dirkwa/signalk-hmi-designer)) can rely on.

Reference implementation: this firmware. Any device that obeys this contract is a JLP device.

## Discovery

A JLP device announces itself on the local network via mDNS:

- **Service:** `_signalk-player._tcp`
- **Port:** the JLP HTTP API port (default 8081 on the reference firmware).
- **TXT records:**
  - `schema=1` — wire protocol version.
  - `widgets=label,toggle,arc,bar` — comma-separated widget kinds the device implements.
  - `firmware=<name>-<version>` — informational.
  - `api=/layout,/hello,/healthz` — informational.

Hosts iterate `_signalk-player._tcp.local` results and confirm capabilities with `GET /hello` before pushing.

## HTTP endpoints

All endpoints live on the same TCP port. The reference firmware uses **8081**.

### `GET /hello`

Capability descriptor. Always returns 200 with a JSON body:

```json
{
  "schema": 1,
  "name": "Main helm",
  "hostname": "p4-cockpit",
  "firmware": "p4-cockpit-jlp-0.1.0",
  "display": { "w": 1024, "h": 600 },
  "widgets": {
    "label":  { "fields": ["x","y","w","h","label","bind","display"] },
    "toggle": { "fields": ["x","y","w","h","label","bind"] },
    "arc":    { "fields": ["x","y","w","h","label","bind","display","min","max","start_angle","end_angle"] },
    "bar":    { "fields": ["x","y","w","h","label","bind","display","min","max","vertical"] }
  },
  "active_layout_name": "Main helm",
  "layout_source": "littlefs"
}
```

Field meanings:

| Field                | Meaning                                                                 |
|----------------------|-------------------------------------------------------------------------|
| `schema`             | Wire schema version. Currently always `1`.                              |
| `name`               | Human name of the device.                                               |
| `hostname`           | Network hostname (matches mDNS).                                        |
| `firmware`           | Firmware build tag.                                                     |
| `display.w` / `.h`   | Pixel dimensions of the renderable area.                                |
| `widgets`            | Dictionary of supported widget kinds. The `fields` list is the set of JSON keys the device recognises for that kind — clients MUST NOT emit unknown keys for a kind.                                                                              |
| `active_layout_name` | The `name` of the currently-rendered layout, or `""` if none.           |
| `layout_source`      | Where the active layout came from: `"littlefs"` \| `"default"` \| `"applicationData"` \| `"post"`. |

Clients SHOULD `GET /hello` before pushing and refuse to emit widgets/fields the device doesn't advertise.

### `POST /layout`

Apply a layout JSON. Request body: a JSON document conforming to the schema below. Max body size: **64 KB**.

The device parses, validates, builds the LVGL tree under a hidden staging parent, then atomically swaps it in. If parse, validate or build fails, the prior layout stays live — **the screen never blanks** — and the device returns a non-200 with an error body. On success the layout is persisted (LittleFS on the reference firmware) so it survives reboot.

Synchronous: the device replies only after the swap completes (typically <1s; absolute upper bound 10s before timing out).

Success response (200):

```json
{ "ok": true, "name": "Main helm", "screens": 2, "widgets": 14 }
```

`warning` MAY be present on success — typically `"persist failed"`, meaning the layout is live in RAM but won't survive reboot.

Failure response (400 / 422 / 500):

```json
{ "ok": false, "err": "screen[0].widgets[3]: unknown type 'nope'" }
```

### `GET /screenshot`

Returns the current framebuffer as a **16-bit RGB565 BMP** — a standard 70-byte BMP header (BITMAPINFOHEADER + BI_BITFIELDS RGB565 masks) followed by bottom-up raw pixels. Designed for the designer's overlay-and-align workflow; not a stable streaming protocol.

Content-Type: `image/bmp`.

### `GET /healthz`

Liveness probe. Returns 200 `{"ok":true}` if the device is up. No body fields are stable beyond `ok`.

## Layout JSON schema

```jsonc
{
  "schema": 1,                                  // required, must be 1
  "name": "Main helm",                          // required, free-form
  "status_overlay": true,                       // optional, default true
  "tab_strip_height": 56,                       // optional, default 56
  "screens": [                                  // required, ≥ 1
    {
      "id": "switches",                         // required, unique within layout
      "title": "SW",                            // optional, tab button text (falls back to id)
      "widgets": [                              // required, ≥ 1
        {
          "type": "toggle",                     // required
          "id": "nav_lights",                   // required, unique within screen
          "label": "Nav lights",                // optional
          "bind": "electrical.switches.bank.0.1.state",  // optional (required for most kinds)
          "x": 20, "y": 20, "w": 220, "h": 100  // optional, see defaults below
        }
      ]
    }
  ]
}
```

### Top-level

| Field             | Type    | Required | Default | Notes                                                |
|-------------------|---------|----------|---------|------------------------------------------------------|
| `schema`          | int     | yes      | —       | MUST be `1`.                                         |
| `name`            | string  | yes      | —       | Display name; returned in `/hello.active_layout_name`. |
| `status_overlay`  | bool    | no       | `true`  | Whether the device's status strip should be visible. |
| `tab_strip_height`| int     | no       | `56`    | Pixel height of the multi-screen tab strip. Ignored when there is only one screen. |
| `screens`         | array   | yes      | —       | At least one screen.                                 |

### Screen object

| Field     | Type    | Required | Notes                                |
|-----------|---------|----------|--------------------------------------|
| `id`      | string  | yes      | Unique within the layout.            |
| `title`   | string  | no       | Tab button text. Falls back to `id`. |
| `widgets` | array   | yes      | At least one widget.                 |

### Common widget fields

| Field      | Type    | Required | Default | Notes                                                  |
|------------|---------|----------|---------|--------------------------------------------------------|
| `type`     | string  | yes      | —       | Widget kind. Must be one the device's `/hello` lists.  |
| `id`       | string  | yes      | —       | Unique within the enclosing screen.                    |
| `x`, `y`   | int     | no       | `0`     | Top-left position in device pixels (origin top-left).  |
| `w`, `h`   | int     | no       | `120`, `60` | Pixel size.                                        |
| `label`    | string  | no       | —       | Caption text. Semantics depend on widget kind.         |
| `bind`     | string  | no       | —       | SignalK path. Required for everything except a label whose `label` is a fixed string. |
| `display`  | object  | no       | —       | Value formatting (see below).                          |
| `bg_color` | string  | no       | theme `#161b22` | Hex color (`#rrggbb` or `#rgb`) for the tile background. **SK zones still win** when the path matches one; this is the fallback. Use it for operator-meaningful fixed colors (STOP=red, ACK=yellow) that should be visible even without zone state. |
| `fg_color` | string  | no       | theme `#e6edf3` / `#58a6ff` | Hex color for the value text (label/bar) or arc indicator. Same zone-wins precedence. |

### `display` object

Used by label / arc / bar to format the bound numeric value.

| Field      | Type   | Default | Notes                                                |
|------------|--------|---------|------------------------------------------------------|
| `unit`     | string | `""`    | Suffix appended after the formatted number.          |
| `scale`    | float  | `1.0`   | Multiplier applied before display.                   |
| `offset`   | float  | `0.0`   | Added after `scale`.                                 |
| `decimals` | int    | `1`     | Decimal places.                                      |

Displayed value = `(raw × scale) + offset`, formatted to `decimals`, then `unit` appended.

### Widget kinds

#### `label`
- Extra fields: none.
- Renders `label` (caption) above the formatted value from `bind`. If only `label` is set, renders a static caption.
- Subject kind: `Float` if `display` set, else `String`.

#### `toggle`
- Extra fields: none.
- Renders a caption (`label`) on the left and an LVGL switch on the right.
- Subject kind: `Int` (treats `0` as off, anything else as on).
- Tap → emits a SignalK PUT to `bind` with the inverted value. The visual switches optimistically and reconciles against the SK echo (~500 ms timeout); if no echo arrives, the switch snaps back to the authoritative subscription value.

#### `arc`
- Extra fields:
  - `min`, `max` (float, required) — value range mapped to the arc sweep.
  - `start_angle` (int, default `135`), `end_angle` (int, default `45`) — degrees; 0° is east, sweep is clockwise.
- Renders a circular arc forced into the largest square that fits inside `w × h`; caption (`label`) and formatted value are centred inside.
- Subject kind: `Float`.

#### `bar`
- Extra fields:
  - `min`, `max` (float, required) — value range.
  - `vertical` (bool, default `false`).
- Subject kind: `Float`.

#### `button` *(reserved)*
- Not yet implemented by the reference firmware (not advertised in `/hello.widgets`). The designer offers it for layout authoring; pushes referencing `button` to a device that doesn't list it will be rejected.

### Validation rules

A layout MUST be rejected if any of the following holds:

1. `schema` is not `1`.
2. `screens` is missing or empty.
3. Two screens share an `id`.
4. Two widgets in the same screen share an `id`.
5. A widget's `type` is not advertised by the device's `/hello`.
6. A widget declares a `bind` whose subject kind conflicts with another widget binding the same path (e.g. one wants `Int`, another wants `Float`).
7. A widget references a field not in `/hello.widgets[type].fields`.

On rejection the device keeps the previously-active layout and returns `400` (or `422` for kind/field validation, `500` for build failures) with a human-readable `err`.

## Boot priority

A JLP device that has just booted resolves which layout to render in this order. The first one that builds successfully wins; the screen never blanks.

1. **`littlefs`** — the most recently persisted layout from a successful `POST /layout`.
2. **`default`** — a compiled-in minimal layout shipped with the firmware.
3. **`applicationData`** *(async, after boot)* — fetched from `http://<sk-host>:<sk-port>/signalk/v1/applicationData/global/<hostname>/1/layout.json`. If present and different from the active layout, it's applied after the synchronous boot path has already put something on screen.

`POST /layout` always takes precedence at runtime and overwrites the persisted layout on success.

## SignalK bindings

The device subscribes to every `bind` path in the active layout over a single SignalK WebSocket. Updates arrive as deltas; the device updates the corresponding widget on the next render tick.

`bind` values follow SignalK convention: dot-separated paths under `vessels.self.`, e.g. `environment.depth.belowKeel`, `electrical.switches.bank.0.1.state`.

### Meta and zones

The device requests meta in-stream (`sendMeta=all`) and uses two kinds of meta per path:

- **`displayUnits` formula** (preferred when available): inferred `scale` / `offset` / `unit` so a widget without an explicit `display` block still formats sensibly.
- **`zones[]`**: array of `{lower?, upper?, state}` entries where `state ∈ {nominal, alert, warn, alarm, emergency}`. Matching is in **raw SK units** (`lower ≤ raw_value < upper`, or `raw_value == lower` when `lower == upper` for point zones on bool/int paths). Zones are first-match in declaration order. Matching widget's background is tinted per the state, using a maritime-helm escalation palette: nominal/normal → green, alert → yellow, warn → orange, alarm → red, emergency → purple. Widgets without zone meta get the theme default. *Note: this palette is one severity step warmer than the SK spec defaults (which puts alert at blue) — chosen so a glance at the helm reads like a traffic light.*

When the layout's explicit `display` block is present it overrides the path's `displayUnits` formula.

## SignalK PUT (`toggle`)

Tap on a `toggle` widget emits:

```
PUT /signalk/v1/api/vessels/self/<dotted bind path>
{ "value": <inverted current> }
```

over the same WebSocket as a SignalK REST request envelope. The device does **not** send a `source` field — if the path has multiple cached sources, SK rejects the PUT; the layout author or operator is expected to clear stale sources (or the source-disambiguation work in [SK PR #2703](https://github.com/SignalK/signalk-server/pull/2703) needs to be live).

The visual flips optimistically on tap and reconciles against the authoritative subscription value 500 ms later. If no echo lands in time the switch snaps back.

## Versioning

Wire schema versions are integers. A schema change that breaks compatibility increments the integer; the device's `/hello.schema` is authoritative. Clients refuse to push to a device whose `schema` they don't understand.

Field additions inside a kind's `fields` list are not breaking — clients ignore unknown fields, devices ignore values they don't recognise. Removing a field, renaming one, or changing its semantics is a schema bump.
