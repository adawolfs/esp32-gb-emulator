#include <Arduino.h>
#include <stdio.h>

#include "cpu.h"
#include "gbrom.h"
#include "lcd.h"
#include "mem.h"
#include "rom.h"
#include "sdl.h"
#include "timer.h"

static constexpr uint32_t FRAME_US = 1000000u / 60u;

void setup() {
  Serial.begin(115200);
  delay(150);

  rom_init(gb_rom);
  sdl_init();
  gameboy_mem_init();
  cpu_init();
  Serial.println("GB ready");
}

void loop() {
  uint32_t frame_start = micros();
  bool screen_updated = false;

  while (!screen_updated) {
    unsigned int cycles = cpu_cycle();
    screen_updated = lcd_cycle(cycles);
    timer_cycle(cycles);
  }

  sdl_update();

  uint32_t elapsed = micros() - frame_start;
  if (elapsed < FRAME_US) delayMicroseconds(FRAME_US - elapsed);
}
