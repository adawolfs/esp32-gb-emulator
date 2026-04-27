#include "emulator_runtime.h"

#include <Arduino.h>

#include "board_config.h"
#include "cpu.h"
#include "display.h"
#include "gbrom.h"
#include "lcd.h"
#include "mem.h"
#include "rom.h"
#include "sdl.h"
#include "timer.h"

bool emulator_init(void) {
  sdl_init();
  display_show_status("Loading ROM", "Pokemon Yellow");

  if (!rom_init(gb_rom)) {
    display_show_status("ROM error", "Invalid header/checksum");
    return false;
  }

  gameboy_mem_init();
  cpu_init();
  display_show_status("GB ready", "Starting");
  return true;
}

void emulator_run_frame(void) {
  const uint32_t frame_start = micros();
  bool screen_updated = false;

  while (!screen_updated) {
    const unsigned int cycles = cpu_cycle();
    if (!cycles) {
      display_show_status("CPU stopped", "Unhandled opcode");
      delay(1000);
      return;
    }
    screen_updated = lcd_cycle(cycles);
    timer_cycle(cycles);
  }

  sdl_update();

  const uint32_t elapsed = micros() - frame_start;
  if (elapsed < board::FRAME_US) {
    delayMicroseconds(board::FRAME_US - elapsed);
  }
}
