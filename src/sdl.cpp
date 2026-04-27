#define LGFX_USE_V1
#include "sdl.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Wire.h>

#include "CST816D.h"

// ---- Pin definitions (matches CST816D-ESP32-C3-BASE hardware) ----
#define PIN_BL   3
#define PIN_DC   2
#define PIN_SCLK 6
#define PIN_MOSI 7
#define PIN_CS   10
#define I2C_SDA  4
#define I2C_SCL  5
#define TP_RST   1
#define TP_INT   0

static constexpr uint32_t TFT_WRITE_HZ = 40000000;

// ---- LovyanGFX + GC9A01 ----
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI      _bus;

 public:
  LGFX() {
    {
      auto cfg       = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = TFT_WRITE_HZ;
      cfg.freq_read  = 20000000;
      cfg.spi_3wire  = false;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk   = PIN_SCLK;
      cfg.pin_mosi   = PIN_MOSI;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = PIN_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg            = _panel.config();
      cfg.pin_cs          = PIN_CS;
      cfg.pin_rst         = -1;
      cfg.pin_busy        = -1;
      cfg.memory_width    = 240;
      cfg.memory_height   = 240;
      cfg.panel_width     = 240;
      cfg.panel_height    = 240;
      cfg.offset_x        = 0;
      cfg.offset_y        = 0;
      cfg.offset_rotation = 0;
      cfg.readable        = false;
      cfg.invert          = false;
      cfg.rgb_order       = true;  // BGR for GC9A01
      cfg.dlen_16bit      = false;
      cfg.bus_shared      = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};

static LGFX tft;
static CST816D touch(I2C_SDA, I2C_SCL, TP_RST, TP_INT);

#define GB_W      160
#define GB_H      144
#define SCR_W     240
#define SCR_H     240
#define X_OFF     ((SCR_W - GB_W) / 2)  // 40
#define Y_OFF     ((SCR_H - GB_H) / 2)  // 48

static uint8_t frame_buf[GB_W * GB_H];
static uint16_t scanout_buf[GB_W * GB_H];
static constexpr uint16_t color_palette[] = {
    0xFFFF,
    static_cast<uint16_t>((16 << 11) | (32 << 5) | 16),
    static_cast<uint16_t>((8 << 11) | (16 << 5) | 8),
    0x0000,
};

// ---- Touch input ----
// Touch zone layout (full 240x240 screen, 3x3 grid of 80px columns, mixed rows):
//
//   x:   0    80   160   240
//        +----+----+----+
//  y< 60 | SL | UP | ST |   SELECT / UP / START
//        +----+----+----+
//  y<160 | LT |    | A  |   LEFT / (game) / A
//        +----+----+----+
//  y<240 | DN | DN | B  |   DOWN / DOWN / B
//        +----+----+----+
//
// RIGHT d-pad: x > 200, y in [60,160] (overlaps A zone — narrow right strip)
//   Override A with RIGHT when x > 200.

static unsigned int touch_buttons = 0;     // start(3) select(2) b(1) a(0)
static unsigned int touch_directions = 0;  // down(3) up(2) left(1) right(0)

static void update_touch() {
  uint16_t tx, ty;
  uint8_t  gesture;
  bool touched = touch.getTouch(&tx, &ty, &gesture);

  touch_buttons    = 0;
  touch_directions = 0;

  if (!touched) return;

  if (tx < 80) {
    if (ty < 60)       touch_buttons    |= (1u << 2); // SELECT
    else if (ty < 160) touch_directions |= (1u << 1); // LEFT
    else               touch_directions |= (1u << 3); // DOWN
  } else if (tx < 160) {
    if (ty < 60)       touch_directions |= (1u << 2); // UP
    else if (ty >= 160) touch_directions |= (1u << 3); // DOWN
    // center 60-160: game area, no input
  } else {
    // x >= 160
    if (ty < 60)       touch_buttons    |= (1u << 3); // START
    else if (tx > 200 && ty < 160)
                       touch_directions |= (1u << 0); // RIGHT (far right strip)
    else if (ty < 160) touch_buttons    |= (1u << 0); // A
    else               touch_buttons    |= (1u << 1); // B
  }
}

unsigned int sdl_get_buttons(void) { return touch_buttons; }
unsigned int sdl_get_directions(void) { return touch_directions; }
uint8_t *sdl_get_framebuffer(void) { return frame_buf; }

void sdl_frame(void) {
  for (int i = 0; i < GB_W * GB_H; ++i) {
    scanout_buf[i] = color_palette[frame_buf[i] & 0x03];
  }

  tft.startWrite();
  tft.setWindow(X_OFF, Y_OFF, X_OFF + GB_W - 1, Y_OFF + GB_H - 1);
  tft.writePixels(reinterpret_cast<lgfx::rgb565_t *>(scanout_buf), GB_W * GB_H);
  tft.endWrite();
}

int sdl_update(void) {
  update_touch();
  sdl_frame();
  return 0;
}

void sdl_quit(void) {}

static void draw_label(int cx, int cy, const char *text, uint32_t color) {
  tft.setTextColor(
      tft.color565(color >> 16, (color >> 8) & 0xFF, color & 0xFF));
  tft.setTextSize(1);
  tft.drawCenterString(text, cx, cy);
}

void sdl_init(void) {
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  touch.begin();

  tft.init();
  tft.setColorDepth(16);
  tft.initDMA();
  tft.fillScreen(TFT_BLACK);

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
