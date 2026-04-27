# CLAUDE.md

Guidance for agents working in this directory.

## Target

This is an ESP32-C3 Game Boy emulator port using:

- GC9A01 round 240x240 TFT over SPI2
- CST816D capacitive touch over I2C
- LovyanGFX 1.2.19
- WebServer + links2004/WebSockets for the embedded control portal
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
- `src/apu.cpp` owns the optional mono APU and PCM sample ring buffer.
- `src/touch_input.cpp` owns CST816D polling and Game Boy input mapping.
- `src/sdl.cpp` is only the compatibility facade expected by the original core.
- `src/web_portal.cpp` owns SoftAP, HTTP routes, WebSocket commands, packed
  framebuffer streaming and optional web-audio PCM streaming.
- `src/mem.cpp` owns memory mapping, cartridge RAM and active ROM-bank pointers.
- `src/mbc.cpp` owns mapper state for MBC1, MBC3 and MBC5.

## Rules

- Keep hardware-specific code out of CPU/LCD/timer/interrupt logic.
- Keep the emulator framebuffer indexed at 160x144 unless you intentionally
  redesign the LCD pipeline.
- Web controls must merge into the same button bitfields as physical touch.
- New button presses should request `INTR_JOYPAD`; some games can wait on the
  joypad interrupt rather than polling continuously.
- Keep browser streaming capped unless profiling on hardware proves there is
  CPU and Wi-Fi headroom.
- Audio is disabled by default with `GB_ENABLE_AUDIO=0` in `board_config.h`
  because the software APU is expensive on ESP32-C3. When enabled, it targets
  Web Audio via WebSocket `GBA` binary chunks. Local PWM/I2S output is a
  separate hardware-output step.
- Do not reintroduce LVGL or TFT_eSPI configuration files.
- Validate changes with `~/.platformio/penv/bin/pio run`.
