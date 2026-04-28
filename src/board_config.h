#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdint.h>

#ifndef GB_ENABLE_AUDIO
#define GB_ENABLE_AUDIO 0
#endif

namespace board {

static constexpr int PIN_BACKLIGHT = 3;
static constexpr int PIN_TFT_DC = 2;
static constexpr int PIN_TFT_SCLK = 6;
static constexpr int PIN_TFT_MOSI = 7;
static constexpr int PIN_TFT_CS = 10;

static constexpr int PIN_TOUCH_SDA = 4;
static constexpr int PIN_TOUCH_SCL = 5;
static constexpr int PIN_TOUCH_RST = 1;
static constexpr int PIN_TOUCH_INT = 0;

static constexpr int GAMEBOY_WIDTH = 160;
static constexpr int GAMEBOY_HEIGHT = 144;
static constexpr int SCREEN_WIDTH = 240;
static constexpr int SCREEN_HEIGHT = 240;
static constexpr int SCREEN_X_OFFSET = (SCREEN_WIDTH - GAMEBOY_WIDTH) / 2;
static constexpr int SCREEN_Y_OFFSET = (SCREEN_HEIGHT - GAMEBOY_HEIGHT) / 2;

static constexpr uint32_t TFT_WRITE_HZ = 40000000;
static constexpr uint32_t TARGET_FPS = 60;
static constexpr uint32_t FRAME_US = 1000000u / TARGET_FPS;
static constexpr uint32_t WEB_PORTAL_IDLE_SERVICE_INTERVAL_MS = 50;
static constexpr uint32_t WEB_INPUT_TIMEOUT_MS = 1500;
static constexpr uint32_t WEB_MIN_PRESS_MS = 180;
static constexpr uint32_t SAVE_FLUSH_DEBOUNCE_MS = 1000;

static constexpr const char *WEB_AP_SSID = "GameBoy-Link";
static constexpr const char *WEB_AP_PASSWORD = "gameboy123";
static constexpr uint16_t WEB_HTTP_PORT = 80;
static constexpr uint16_t WEB_SOCKET_PORT = 81;
static constexpr uint16_t WEB_STREAM_INTERVAL_MS = 100;
static constexpr bool AUDIO_ENABLED = GB_ENABLE_AUDIO != 0;
static constexpr bool WEB_AUDIO_ENABLED = AUDIO_ENABLED;
static constexpr bool DEBUG_TOUCH_INPUT = false;
static constexpr bool DEBUG_WEB_INPUT = false;
static constexpr bool DEBUG_INTERRUPTS = false;

static constexpr uint16_t DMG_PALETTE[4] = {
    0xFFFF,
    static_cast<uint16_t>((16 << 11) | (32 << 5) | 16),
    static_cast<uint16_t>((8 << 11) | (16 << 5) | 8),
    0x0000,
};

}  // namespace board

#endif
