# Firmware and Runtime

## Table of Contents

- [Boot Sequence](#boot-sequence)
- [Frame Loop](#frame-loop)
- [Memory Lifecycle](#memory-lifecycle)
- [Configuration Knobs](#configuration-knobs)
- [Failure Modes](#failure-modes)

## Boot Sequence

`src/main.cpp` keeps the Arduino wrapper intentionally small:

1. Start serial.
2. Call `emulator_init()`.
3. If initialization succeeds, enter the frame loop.

`emulator_init()` performs the actual system boot:

1. Initialize display and touch.
2. Start the Wi-Fi access point and web services.
3. Validate the ROM header and checksum.
4. Allocate emulator RAM and cartridge RAM.
5. Initialize CPU state.

Initialization failures are surfaced to the display so the board can fail visibly instead of silently.

## Frame Loop

`emulator_run_frame()` is the main runtime scheduler.

The current flow is:

1. Service the web portal once at frame start.
2. Execute CPU cycles until the LCD pipeline reports a completed screen refresh.
3. Advance timer logic and optional audio logic for every CPU slice.
4. Push the final framebuffer to the LCD and web portal.
5. Delay to maintain the configured target frame time.

This structure favors stable frame pacing. Web services are handled at frame boundaries to avoid stealing too much time from CPU emulation on the ESP32-C3.

## Memory Lifecycle

`gameboy_mem_init()` now returns `bool` and explicitly validates memory allocation.

Behavior:

- system RAM is allocated as a 64 KiB flat map
- cartridge RAM is allocated according to the ROM header
- previously allocated buffers are released before reinitialization
- battery-backed cartridge RAM is restored from `/sram.bin` in SPIFFS when available, and is flushed back the moment the game disables cart RAM (the canonical save-complete signal), with a 1 s debounce as a backstop
- failure returns are propagated to `emulator_init()`

This keeps the startup path explicit and removes silent allocation failure behavior.

## Configuration Knobs

Most project-wide runtime constants live in `src/board_config.h`.

Important values:

- `TARGET_FPS`
- `FRAME_US`
- `WEB_STREAM_INTERVAL_MS`
- `WEB_INPUT_TIMEOUT_MS`
- `WEB_MIN_PRESS_MS`
- `DEBUG_TOUCH_INPUT`
- `DEBUG_WEB_INPUT`
- `DEBUG_INTERRUPTS`

The intended pattern is:

- timing defaults in `board_config.h`
- module code referencing those constants instead of duplicating literals

## Failure Modes

Current initialization failures that are handled explicitly:

- Wi-Fi AP creation failure
- invalid ROM header or checksum
- RAM allocation failure
- CPU halt on unhandled opcode

Current failure modes that still remain largely diagnostic:

- touch I2C read anomalies
- browser disconnects during streaming
- save loss if power is removed before the SRAM debounce flush completes
