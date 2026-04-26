# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash Commands

```bash
# Build firmware
pio run

# Build and upload to connected ESP32-C3
pio run --target upload

# Open serial monitor (115200 baud)
pio device monitor

# Build + upload + monitor in one shot
pio run --target upload && pio device monitor

# Clean build artifacts
pio run --target clean
```

There are no automated tests — this is embedded firmware targeting physical hardware.

## Hardware Target

| Item | Value |
|------|-------|
| MCU | ESP32-C3 (RISC-V, single-core, ~384KB RAM) |
| Board | `esp32-c3-devkitm-1` |
| Display | GC9A01 round 240×240 TFT via SPI2 |
| Touch | CST816D capacitive touch (I2C, addr 0x15) |
| Framework | Arduino via PlatformIO |

**Pin map:**

| Signal | GPIO |
|--------|------|
| SPI MOSI | 7 |
| SPI SCLK | 6 |
| TFT CS | 10 |
| TFT DC | 2 |
| Backlight | 3 |
| I2C SDA (touch) | 4 |
| I2C SCL (touch) | 5 |
| Touch INT | 0 |
| Touch RST | 1 |

## Architecture

The project is a single-file Arduino sketch (`src/main.cpp`) with one supporting driver (`src/CST816D.{h,cpp}`).

### Display Stack

LovyanGFX (v1) drives the GC9A01 panel via SPI2 with DMA. The `LGFX` class defined inline in `main.cpp` configures the bus and panel. LVGL 8.3.11 sits on top and renders to a double-buffer (`buf1`/`buf2`, 60 lines × 240 px each) using the `my_disp_flush` callback, which calls `pushImageDMA` + `waitDMA`. LVGL uses native RGB565 (`LV_COLOR_16_SWAP = 0`), and the flush callback passes the buffer as `lgfx::rgb565_t` so LovyanGFX handles the SPI byte order.

LVGL's 1ms tick is driven by an `esp_timer` periodic timer (not `millis()`). `lv_timer_handler()` is called in `loop()` every ~3ms.

### Touch Driver

`CST816D` communicates over I2C using raw register reads. `begin()` pulses the INT and RST pins to wake the controller and writes register `0xFE = 0xFF` to disable auto-sleep. `getTouch()` reads finger count, gesture, and X/Y coordinates. Only `SlideUp`/`SlideDown` gestures are forwarded; all others are mapped to `None`.

### UI

`create_ui()` builds a round-display demo with three tap-cycled modes:
- **CORE** — main status view with power/sync/heap telemetry
- **SCAN** — field scan view with animated sweep line and scan/noise/lock stats
- **LINK** — link status view with TX/RX/heap telemetry

The widget tree uses three `lv_arc` rings, a generated LVGL-native RGB565 pixel-art image with chroma-key transparency, three compact `lv_bar` status chips, an `lv_chart` waveform, a mode badge, labels, and four orbit dots. `demo_timer_cb()` runs every 120ms and calls `update_demo_values()` to simulate telemetry and update rings, bars, chart, labels, and dot positions. `apply_mode()` owns all mode-specific colors, labels, and visibility.

### Memory Constraints

ESP32-C3 has limited RAM. LVGL heap is capped at 48KB (`LV_MEM_SIZE`). Display buffers are `DMA_ATTR` to keep them in DMA-accessible memory. Only the widgets used by the demo are enabled in `include/lv_conf.h` (`LABEL`, `BTN`, `ARC`, `BAR`, `IMG`, `CHART`).

## Key Configuration Files

- **`platformio.ini`** — board, framework, lib deps, and build flags. `-D LV_CONF_INCLUDE_SIMPLE` tells LVGL to find `lv_conf.h` on the include path.
- **`include/lv_conf.h`** — LVGL feature flags and memory budget. Enable additional widgets here (`LV_USE_CHART`, `LV_USE_SPINNER`, etc.) as needed.
- **`include/User_Setup.h`** — legacy TFT_eSPI pin config reference only. The firmware no longer depends on TFT_eSPI; LovyanGFX is the actual render driver.

## Common Pitfalls

- **LovyanGFX version**: keep `LovyanGFX@1.2.19` pinned. `^1.2.19` can resolve to 1.2.20, which conflicts with this LVGL setup.
- **TFT_eSPI**: do not add it back unless switching drivers. It is unused here and can slow/fail PlatformIO dependency scanning.
- **SPI frequency**: currently 40MHz through `TFT_WRITE_HZ`. If display shows glitches, lower it to 27MHz.
- **Color inversion / RGB order**: `PANEL_INVERT_COLOR` and `PANEL_BGR_ORDER` are module-specific. Toggle these first if colors appear wrong.
- **X-axis flip**: uncomment `*x = 240 - *x;` in `CST816D::getTouch()` if touch coordinates are mirrored.
