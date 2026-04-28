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

namespace {

uint32_t last_web_service_ms = 0;

WebPortalConfig make_portal_config() {
  WebPortalConfig portal_config;
  portal_config.ap_ssid = board::WEB_AP_SSID;
  portal_config.ap_password = board::WEB_AP_PASSWORD;
  portal_config.http_port = board::WEB_HTTP_PORT;
  portal_config.websocket_port = board::WEB_SOCKET_PORT;
  portal_config.stream_interval_ms = board::WEB_STREAM_INTERVAL_MS;
  return portal_config;
}

void service_web_portal(uint32_t now_ms, const uint8_t *framebuffer) {
  const bool has_clients = web_portal_client_count() > 0;
  if (last_web_service_ms != 0 && !has_clients &&
      now_ms - last_web_service_ms < board::WEB_PORTAL_IDLE_SERVICE_INTERVAL_MS) {
    return;
  }

  web_portal_loop(now_ms, framebuffer);
  last_web_service_ms = now_ms;
}

}  // namespace

bool emulator_init(void) {
  sdl_init();
  display_show_status("Starting WiFi", board::WEB_AP_SSID);

  if (!web_portal_begin(make_portal_config())) {
    display_show_status("WiFi error", "AP start failed");
    return false;
  }

  display_show_status("Loading ROM", web_portal_ip());

  if (!rom_init(gb_rom)) {
    display_show_status("ROM error", "Invalid header/checksum");
    return false;
  }

  if (!gameboy_mem_init()) {
    display_show_status("Memory error", "Allocation failed");
    return false;
  }
  cpu_init();
  display_show_status("GB ready", "Starting");
  return true;
}

void emulator_run_frame(void) {
  const uint32_t frame_start_us = micros();

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
  }

  sdl_update();
  service_web_portal(millis(), sdl_get_framebuffer());

  const uint32_t elapsed = micros() - frame_start_us;
  if (elapsed < board::FRAME_US) {
    delayMicroseconds(board::FRAME_US - elapsed);
  }
}
