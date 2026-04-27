# CLAUDE.md

Guidance for agents working in this directory.

## Target

This is an ESP32-C3 Game Boy emulator port using:

- GC9A01 round 240x240 TFT over SPI2
- CST816D capacitive touch over I2C
- LovyanGFX 1.2.19
- PlatformIO Arduino framework

It is not an LVGL project and it does not use TFT_eSPI.

## Commands

```bash
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run --target upload
~/.platformio/penv/bin/pio device monitor
```

## Architecture

- `src/main.cpp` should stay minimal.
- `src/emulator_runtime.cpp` owns ROM/core initialization and one-frame execution.
- `src/board_config.h` owns board pins, dimensions, timing and palette constants.
- `src/display.cpp` owns LovyanGFX/GC9A01 setup and frame presentation.
- `src/touch_input.cpp` owns CST816D polling and Game Boy input mapping.
- `src/sdl.cpp` is only the compatibility facade expected by the original core.
- `src/mem.cpp` owns memory mapping, cartridge RAM and active ROM-bank pointers.
- `src/mbc.cpp` owns mapper state for MBC1, MBC3 and MBC5.

## Rules

- Keep hardware-specific code out of CPU/LCD/timer/interrupt logic.
- Keep the emulator framebuffer indexed at 160x144 unless you intentionally
  redesign the LCD pipeline.
- Do not reintroduce LVGL or TFT_eSPI configuration files.
- Validate changes with `~/.platformio/penv/bin/pio run`.
