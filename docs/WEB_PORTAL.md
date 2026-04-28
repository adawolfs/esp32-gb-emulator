# Web Portal and Input

## Table of Contents

- [Network Model](#network-model)
- [Static UI Build](#static-ui-build)
- [HTTP Endpoints](#http-endpoints)
- [WebSocket Protocol](#websocket-protocol)
- [Input Pipeline](#input-pipeline)
- [UI Notes](#ui-notes)
- [Debugging](#debugging)

## Network Model

The board exposes a local access point and serves both the UI and the control APIs from the ESP32 itself.

Defaults:

- SSID: `GameBoy-Link`
- password: `gameboy123`
- HTTP port: `80`
- WebSocket port: `81`

The browser connects to the AP, loads `/`, then fetches `/api/state` and opens the WebSocket.

## Static UI Build

The browser UI is no longer embedded in firmware source.

Current flow:

- source app lives in `webui/`
- Vite builds a lightweight installable PWA bundle into `data/`
- PlatformIO packs `data/` into a SPIFFS image
- ESP32 serves the site from SPIFFS at runtime

Relevant commands:

- `npm --prefix webui install`
- `npm --prefix webui run build`
- `~/.platformio/penv/bin/pio run --target buildfs`
- `~/.platformio/penv/bin/pio run --target uploadfs`

Helper scripts:

- `scripts/build_web_ui.sh`
- `scripts/upload_web_ui.sh`
- `scripts/deploy_all.sh`

The visual shell is implemented with HTML/CSS rather than serving the large
reference PNG/SVG artwork.

## HTTP Endpoints

| Route | Method | Purpose |
| --- | --- | --- |
| `/` | `GET` | Serves the embedded HTML UI. |
| `/api/state` | `GET` | Returns current runtime state as JSON. |
| `/api/input` | `POST` | Accepts form-style control updates. |
| `/api/input_state` | `GET` | Alias-style state query for diagnostics. |

The browser now keeps HTTP input fallback enabled when the WebSocket is not open, which aligns implementation with the documented API surface.

## WebSocket Protocol

Text frames:

```json
{"type":"input","control":"a","pressed":true}
```

```json
{"type":"audio","enabled":true}
```

Binary frames:

- `GBF`: packed framebuffer stream
- `GBA`: mono PCM audio chunk

The web portal also broadcasts state JSON after input changes and connection state changes.

## Input Pipeline

The project merges two sources:

- physical touch from `CST816D`
- remote input from the browser

Flow:

1. Browser sends `input` messages by WebSocket or falls back to HTTP.
2. `web_portal.cpp` parses the message and forwards it to `touch_input_set_web_control()`.
3. `touch_input.cpp` updates remote button and direction bitfields.
4. Physical touch is sampled through `touch_input_update()`.
5. `touch_input_buttons()` and `touch_input_directions()` return the merged state.
6. New presses trigger a joypad interrupt.
7. `mem_get_joypad_register()` exposes the resulting state to the emulator core.

Important behavior:

- web input timeout protects against stuck remote buttons
- minimum press duration reduces missed taps on fast network round-trips
- timeout and minimum press are configured centrally in `board_config.h`
- SRAM save persistence happens in firmware and does not depend on the portal being open

## UI Notes

The current portal is intentionally mobile-first and styled after a DMG shell.

Constraints worth preserving during UI changes:

- keep `#screen`, `#status`, and `#meta`
- keep `data-control` values unchanged
- keep canvas at native `160x144`
- avoid layouts that clip rotated `A/B` controls on narrow phones

## Debugging

Touch and web input logging is available through compile-time flags in `src/board_config.h`:

- `DEBUG_TOUCH_INPUT`
- `DEBUG_WEB_INPUT`
- `DEBUG_INTERRUPTS`

Recommended usage:

1. Enable only the flag you need.
2. Rebuild and flash.
3. Inspect the serial monitor at `115200`.

This keeps the default runtime quiet while preserving targeted diagnostics when needed.
