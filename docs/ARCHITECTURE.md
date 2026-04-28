# Architecture

## Table of Contents

- [Overview](#overview)
- [Module Map](#module-map)
- [Execution Layers](#execution-layers)
- [Project Conventions](#project-conventions)
- [Known Boundaries](#known-boundaries)

## Overview

This project is a Game Boy/DMG emulator port for an ESP32-C3 board with:

- a 240x240 GC9A01 display
- a CST816D touch controller
- an embedded ROM image in `src/gbrom.h`
- a local Wi-Fi access point with a web control portal

The codebase combines an existing emulator core with hardware adaptation and a lightweight remote control/streaming layer.

## Module Map

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | Arduino entrypoint and runtime bootstrap. |
| `src/emulator_runtime.cpp` | High-level system initialization and per-frame scheduling. |
| `src/board_config.h` | Hardware pins, timing, web defaults, debug switches. |
| `src/display.cpp` | GC9A01 initialization and framebuffer presentation. |
| `src/touch_input.cpp` | Physical touch mapping and web input merge logic. |
| `src/web_portal.cpp` | SoftAP, HTTP API, WebSocket state/control, browser UI. |
| `src/sdl.cpp` | Compatibility shim used by the original emulator core. |
| `src/mem.cpp` | Game Boy memory map, RAM banking, DMA, joypad register. |
| `src/rom.cpp` | ROM header validation and cartridge metadata decoding. |
| `src/mbc.cpp` | MBC bank-switching logic. |
| `src/cpu.cpp` | CPU execution and opcode dispatch. |
| `src/lcd.cpp` | PPU/LCD state machine and indexed framebuffer generation. |
| `src/timer.cpp` | DIV/TIMA behavior. |
| `src/interrupt.cpp` | IF/IE handling and interrupt dispatch. |
| `src/apu.cpp` | Optional software audio path. |
| `src/CST816D.cpp` | Touch controller driver wrapper around `Wire`. |

## Execution Layers

1. `setup()` calls `emulator_init()`.
2. Hardware display and touch are initialized through the SDL compatibility layer.
3. The device starts a SoftAP and the web portal.
4. ROM header validation and emulator memory initialization are performed.
5. `loop()` runs `emulator_run_frame()` continuously.
6. Each frame executes CPU cycles until `lcd_cycle()` reports a completed frame.
7. The rendered framebuffer is pushed to both the physical display and, periodically, to the web client.

## Project Conventions

- Hardware and runtime constants should live in `src/board_config.h`.
- Debug traces that are not fatal should be gated by compile-time booleans.
- Frame-critical code should avoid heap allocation after startup.
- The browser UI must keep stable DOM ids and `data-control` attributes because the JavaScript control path depends on them.
- The emulator core remains mostly C-like; board adaptation code is where C++ conveniences are preferred.

## Known Boundaries

- The embedded ROM approach is convenient for a fixed demo image but not suitable for general cartridge loading.
- Battery-backed cartridge RAM is persisted to `/sram.bin` on the SPIFFS partition and restored at boot. A flush is triggered immediately when the game disables cart RAM, with a 1 s debounce as a backstop.
- The audio path is optional and still expensive on ESP32-C3.
- The emulator targets DMG behavior, not full CGB compatibility.
