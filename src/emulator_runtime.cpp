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
#include "web_portal.h"

#if GB_ENABLE_AUDIO
#include "apu.h"
#endif

bool emulator_init(void) {
  sdl_init();
  display_show_status("Starting WiFi", board::WEB_AP_SSID);

  WebPortalConfig portal_config;
  portal_config.ap_ssid = board::WEB_AP_SSID;
  portal_config.ap_password = board::WEB_AP_PASSWORD;
  portal_config.http_port = board::WEB_HTTP_PORT;
  portal_config.websocket_port = board::WEB_SOCKET_PORT;
  portal_config.stream_interval_ms = board::WEB_STREAM_INTERVAL_MS;
  web_portal_begin(portal_config);

  display_show_status("Loading ROM", web_portal_ip());

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
  uint32_t last_web_service_ms = millis();
  web_portal_loop(millis(), sdl_get_framebuffer());

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
#if GB_ENABLE_AUDIO
    apu_cycle(cycles);
#endif

    // const uint32_t now_ms = millis();
    // if (now_ms - last_web_service_ms >= 4) {
    //   web_portal_loop(now_ms, nullptr);
    //   last_web_service_ms = now_ms;
    // }
  }

  sdl_update();
  web_portal_loop(millis(), sdl_get_framebuffer());

  const uint32_t elapsed = micros() - frame_start;
  if (elapsed < board::FRAME_US) {
    delayMicroseconds(board::FRAME_US - elapsed);
  }
}
