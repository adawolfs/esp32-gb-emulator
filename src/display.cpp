#define LGFX_USE_V1
#include "display.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "board_config.h"

class DisplayDevice : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel_;
  lgfx::Bus_SPI bus_;

 public:
  DisplayDevice() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = board::TFT_WRITE_HZ;
      cfg.freq_read = 20000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = board::PIN_TFT_SCLK;
      cfg.pin_mosi = board::PIN_TFT_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = board::PIN_TFT_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = board::PIN_TFT_CS;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.memory_width = board::SCREEN_WIDTH;
      cfg.memory_height = board::SCREEN_HEIGHT;
      cfg.panel_width = board::SCREEN_WIDTH;
      cfg.panel_height = board::SCREEN_HEIGHT;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = true;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    setPanel(&panel_);
  }
};

static DisplayDevice tft;
static uint16_t scanout_buf[board::GAMEBOY_WIDTH * board::GAMEBOY_HEIGHT];
static bool overlay_dirty = false;

static void draw_label(int cx, int cy, const char *text, uint32_t color) {
  tft.setTextColor(
      tft.color565(color >> 16, (color >> 8) & 0xFF, color & 0xFF));
  tft.setTextSize(1);
  tft.drawCenterString(text, cx, cy);
}

static void draw_touch_overlay() {
  tft.setFont(&fonts::Font0);
  draw_label(40, 28, "SEL", 0x888888);
  draw_label(120, 28, "UP", 0x888888);
  draw_label(200, 28, "ST", 0x888888);

  tft.setTextColor(TFT_DARKGREY);
  tft.drawCenterString("<", 20, 106);
  draw_label(175, 106, "A", 0x888888);
  tft.setTextColor(TFT_DARKGREY);
  tft.drawCenterString(">", 220, 106);
  draw_label(120, 210, "DN", 0x888888);
  draw_label(200, 210, "B", 0x888888);
}

void display_init(void) {
  pinMode(board::PIN_BACKLIGHT, OUTPUT);
  digitalWrite(board::PIN_BACKLIGHT, HIGH);

  tft.init();
  tft.setColorDepth(16);
  tft.initDMA();
  tft.fillScreen(TFT_BLACK);
  draw_touch_overlay();
}

void display_present_indexed_frame(const uint8_t *framebuffer) {
  if (overlay_dirty) {
    draw_touch_overlay();
    overlay_dirty = false;
  }

  const uint8_t *src = framebuffer;
  uint16_t *dst = scanout_buf;
  const uint16_t *palette = board::DMG_PALETTE;
  const uint8_t *end = framebuffer + (board::GAMEBOY_WIDTH * board::GAMEBOY_HEIGHT);

  while (src < end) {
    *dst++ = palette[*src++ & 0x03];
  }

  tft.startWrite();
  tft.setWindow(board::SCREEN_X_OFFSET, board::SCREEN_Y_OFFSET,
                board::SCREEN_X_OFFSET + board::GAMEBOY_WIDTH - 1,
                board::SCREEN_Y_OFFSET + board::GAMEBOY_HEIGHT - 1);
  tft.writePixels(reinterpret_cast<lgfx::rgb565_t *>(scanout_buf),
                  board::GAMEBOY_WIDTH * board::GAMEBOY_HEIGHT);
  tft.endWrite();
}

void display_show_status(const char *line1, const char *line2) {
  tft.fillScreen(TFT_BLACK);
  overlay_dirty = true;
  tft.setFont(&fonts::Font0);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.drawCenterString(line1 ? line1 : "", board::SCREEN_WIDTH / 2, 104);
  if (line2) {
    tft.setTextColor(TFT_DARKGREY);
    tft.drawCenterString(line2, board::SCREEN_WIDTH / 2, 124);
  }
}
