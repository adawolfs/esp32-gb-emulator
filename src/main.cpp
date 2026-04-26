#define LGFX_USE_V1
#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <Wire.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include "CST816D.h"
#include "esp_timer.h"

// -------------------- Pins --------------------
#define I2C_SDA 4
#define I2C_SCL 5
#define TP_INT 0
#define TP_RST 1

// Backlight (si tu módulo lo usa en GPIO3)
#define PIN_BL 3

// -------------------- Display color tuning --------------------
// GC9A01 escribe RGB565 por SPI. Mantener estos valores juntos evita mezclar
// problemas de panel, bus y formato LVGL.
static constexpr uint32_t TFT_WRITE_HZ = 40000000;
static constexpr bool PANEL_INVERT_COLOR = false;
static constexpr bool PANEL_BGR_ORDER = true; // LovyanGFX: true = BGR para GC9A01.

// -------------------- LVGL Buffer --------------------
// Ojo: ESP32-C3 tiene RAM limitada. 60 suele ir fino y deja espacio para UI compleja.
static const uint32_t screenWidth  = 240;
static const uint32_t screenHeight = 240;
#define BUF_LINES 60

static lv_disp_draw_buf_t draw_buf;
// DMA_ATTR ayuda a que el buffer quede en memoria apta para DMA (depende del core, pero suele ayudar).
static lv_color_t buf1[screenWidth * BUF_LINES] DMA_ATTR;
static lv_color_t buf2[screenWidth * BUF_LINES] DMA_ATTR;

// -------------------- Display (LovyanGFX) --------------------
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_GC9A01 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

public:
  LGFX(void)
  {
    { // Bus config
      auto cfg = _bus_instance.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;

      cfg.freq_write = TFT_WRITE_HZ;
      cfg.freq_read  = 20000000;

      // IMPORTANTE: en 4-wire SPI con DC pin -> false
      cfg.spi_3wire  = false;

      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;

      cfg.pin_sclk   = 6;
      cfg.pin_mosi   = 7;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 2;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // Panel config
      auto cfg = _panel_instance.config();
      cfg.pin_cs   = 10;
      cfg.pin_rst  = -1;
      cfg.pin_busy = -1;

      cfg.memory_width  = 240;
      cfg.memory_height = 240;
      cfg.panel_width   = 240;
      cfg.panel_height  = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;

      cfg.readable = false;
      cfg.invert   = PANEL_INVERT_COLOR;
      cfg.rgb_order = PANEL_BGR_ORDER;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;

      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};

LGFX tft;
CST816D touch(I2C_SDA, I2C_SCL, TP_RST, TP_INT);

// -------------------- LVGL tick timer --------------------
static void lv_tick_task(void *arg) {
  (void)arg;
  lv_tick_inc(1);
}

static esp_timer_handle_t lvgl_tick_timer = nullptr;

// -------------------- LVGL Display flush (DMA-safe) --------------------
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  const int32_t w = (area->x2 - area->x1 + 1);
  const int32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();

  // LVGL entrega RGB565 nativo; LovyanGFX convierte al orden de bytes del bus.
  tft.pushImageDMA(area->x1, area->y1, w, h,
                   reinterpret_cast<const lgfx::rgb565_t *>(&color_p->full));
  tft.waitDMA();

  tft.endWrite();

  lv_disp_flush_ready(disp);
}

// -------------------- Touch --------------------
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
  (void) indev_driver;

  bool touched;
  uint8_t gesture;
  uint16_t touchX;
  uint16_t touchY;

  touched = touch.getTouch(&touchX, &touchY, &gesture);

  if (!touched) {
    data->state = LV_INDEV_STATE_REL;
  } else {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  }
}

// -------------------- Demo UI model --------------------
enum DemoMode : uint8_t {
  MODE_CORE = 0,
  MODE_SCAN,
  MODE_LINK,
  MODE_COUNT
};

struct DemoTheme {
  const char *code;
  const char *title;
  const char *subtitle;
  const char *hint;
  uint32_t bg_top;
  uint32_t bg_bottom;
  uint32_t panel;
  uint32_t accent;
  uint32_t accent_alt;
  uint32_t text;
};

static const DemoTheme DEMO_THEMES[MODE_COUNT] = {
  { "CORE", "DGV-01", "DIGI CORE ONLINE", "tap: field scan", 0x031317, 0x12091B, 0x08242A, 0x16E8D0, 0xFFB000, 0xE8FFFA },
  { "SCAN", "FIELD MAP", "SIGNAL SWEEP ACTIVE", "tap: link sync", 0x060E1A, 0x171026, 0x0B1E36, 0x53A8FF, 0xFF3D8B, 0xEAF4FF },
  { "LINK", "PAIR LINK", "SYNC CHANNEL LOCKED", "tap: digi core", 0x07140E, 0x12150A, 0x102414, 0xA6FF4D, 0xFF7A1A, 0xF2FFE8 }
};

static const char *STAT_NAMES[MODE_COUNT][3] = {
  { "PWR",  "SYNC",  "HEAP" },
  { "SCAN", "NOISE", "LOCK" },
  { "TX",   "RX",    "HEAP" }
};

static lv_obj_t *mode_badge = nullptr;
static lv_obj_t *mode_label = nullptr;
static lv_obj_t *title_label = nullptr;
static lv_obj_t *subtitle_label = nullptr;
static lv_obj_t *hint_label = nullptr;
static lv_obj_t *core_img = nullptr;
static lv_obj_t *outer_arc = nullptr;
static lv_obj_t *inner_arc = nullptr;
static lv_obj_t *pulse_arc = nullptr;
static lv_obj_t *chart = nullptr;
static lv_obj_t *scan_sweep = nullptr;
static lv_obj_t *orbit_dot[4] = { nullptr, nullptr, nullptr, nullptr };
static lv_obj_t *stat_chip[3] = { nullptr, nullptr, nullptr };
static lv_obj_t *stat_name_label[3] = { nullptr, nullptr, nullptr };
static lv_obj_t *stat_value_label[3] = { nullptr, nullptr, nullptr };
static lv_obj_t *stat_bar[3] = { nullptr, nullptr, nullptr };
static lv_chart_series_t *wave_series = nullptr;

static DemoMode active_mode = MODE_CORE;
static float demo_phase = 0.0f;
static uint8_t text_update_divider = 0;

// -------------------- Small helpers --------------------
static uint8_t clamp_percent(int32_t value)
{
  if (value < 0) return 0;
  if (value > 100) return 100;
  return static_cast<uint8_t>(value);
}

static bool in_ellipse(int32_t x, int32_t y, int32_t cx, int32_t cy, int32_t rx, int32_t ry)
{
  const int32_t dx = x - cx;
  const int32_t dy = y - cy;
  return (dx * dx * ry * ry + dy * dy * rx * rx) <= (rx * rx * ry * ry);
}

static void clear_obj_interaction(lv_obj_t *obj)
{
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

static void style_status_chip(lv_obj_t *chip)
{
  clear_obj_interaction(chip);
  lv_obj_set_style_radius(chip, 7, 0);
  lv_obj_set_style_pad_all(chip, 3, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_70, 0);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_set_style_shadow_width(chip, 8, 0);
  lv_obj_set_style_shadow_opa(chip, LV_OPA_20, 0);
}

static lv_obj_t *create_centered_label(lv_obj_t *parent, const char *text, const lv_font_t *font)
{
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  clear_obj_interaction(label);
  return label;
}

// -------------------- LVGL-native pixel asset --------------------
static void build_digicore_img(lv_img_dsc_t &dsc)
{
  static constexpr int32_t W = 84;
  static constexpr int32_t H = 84;
  static lv_color_t pixels[W * H];
  static bool built = false;

  if (!built) {
    const lv_color_t transparent = LV_COLOR_CHROMA_KEY;
    const lv_color_t outline = lv_color_hex(0x031419);
    const lv_color_t body = lv_color_hex(0x2DDBC8);
    const lv_color_t shade = lv_color_hex(0x0B766F);
    const lv_color_t glow = lv_color_hex(0xA9FFF5);
    const lv_color_t screen = lv_color_hex(0x071D24);
    const lv_color_t amber = lv_color_hex(0xFFD35A);
    const lv_color_t pink = lv_color_hex(0xFF4D8F);

    for (int32_t y = 0; y < H; ++y) {
      for (int32_t x = 0; x < W; ++x) {
        lv_color_t px = transparent;

        const bool halo = in_ellipse(x, y, 42, 43, 38, 36);
        const bool outer =
          in_ellipse(x, y, 42, 45, 33, 32) ||
          in_ellipse(x, y, 19, 30, 10, 13) ||
          in_ellipse(x, y, 65, 30, 10, 13);
        const bool inner =
          in_ellipse(x, y, 42, 45, 29, 28) ||
          in_ellipse(x, y, 20, 31, 7, 9) ||
          in_ellipse(x, y, 64, 31, 7, 9);

        if (halo) px = lv_color_hex(0x06242A);
        if (outer) px = outline;
        if (inner) {
          const uint8_t mix = y > 42 ? static_cast<uint8_t>((y - 42) * 6) : 0;
          px = lv_color_mix(shade, body, mix > 190 ? 190 : mix);
        }

        const bool face_panel = x >= 24 && x <= 60 && y >= 43 && y <= 65;
        if (face_panel) px = screen;

        const bool left_eye = x >= 29 && x <= 36 && y >= 35 && y <= 41;
        const bool right_eye = x >= 48 && x <= 55 && y >= 35 && y <= 41;
        if (left_eye || right_eye) px = outline;

        const bool left_eye_glint = x >= 31 && x <= 32 && y >= 36 && y <= 37;
        const bool right_eye_glint = x >= 50 && x <= 51 && y >= 36 && y <= 37;
        if (left_eye_glint || right_eye_glint) px = glow;

        const bool cheek_l = in_ellipse(x, y, 23, 47, 4, 3);
        const bool cheek_r = in_ellipse(x, y, 61, 47, 4, 3);
        if (cheek_l || cheek_r) px = pink;

        const bool amber_core = in_ellipse(x, y, 42, 55, 10, 7);
        if (amber_core) px = amber;

        const bool core_cut = x >= 36 && x <= 48 && y >= 54 && y <= 57;
        if (core_cut) px = lv_color_hex(0xFFF3A8);

        const bool antenna_l = (x >= 29 && x <= 31 && y >= 11 && y <= 24) || in_ellipse(x, y, 29, 10, 4, 4);
        const bool antenna_r = (x >= 53 && x <= 55 && y >= 11 && y <= 24) || in_ellipse(x, y, 55, 10, 4, 4);
        if (antenna_l || antenna_r) px = y < 13 ? amber : outline;

        pixels[y * W + x] = px;
      }
    }

    built = true;
  }

  dsc.header.always_zero = 0;
  dsc.header.w = W;
  dsc.header.h = H;
  dsc.header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED;
  dsc.data_size = W * H * sizeof(lv_color_t);
  dsc.data = reinterpret_cast<const uint8_t *>(pixels);
}

// -------------------- Demo theme and updates --------------------
static void apply_mode()
{
  const DemoTheme &theme = DEMO_THEMES[active_mode];

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(scr, lv_color_hex(theme.bg_top), 0);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(theme.bg_bottom), 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

  lv_label_set_text(mode_label, theme.code);
  lv_label_set_text(title_label, theme.title);
  lv_label_set_text(subtitle_label, theme.subtitle);
  lv_label_set_text(hint_label, theme.hint);

  lv_obj_set_style_text_color(mode_label, lv_color_hex(theme.bg_top), 0);
  lv_obj_set_style_text_color(title_label, lv_color_hex(theme.text), 0);
  lv_obj_set_style_text_color(subtitle_label, lv_color_hex(theme.accent), 0);
  lv_obj_set_style_text_color(hint_label, lv_color_hex(theme.text), 0);

  lv_obj_set_style_bg_color(mode_badge, lv_color_hex(theme.accent), 0);
  lv_obj_set_style_shadow_color(mode_badge, lv_color_hex(theme.accent), 0);

  lv_obj_set_style_arc_color(outer_arc, lv_color_hex(theme.panel), LV_PART_MAIN);
  lv_obj_set_style_arc_color(outer_arc, lv_color_hex(theme.accent), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(inner_arc, lv_color_hex(theme.panel), LV_PART_MAIN);
  lv_obj_set_style_arc_color(inner_arc, lv_color_hex(theme.accent_alt), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(pulse_arc, lv_color_hex(theme.panel), LV_PART_MAIN);
  lv_obj_set_style_arc_color(pulse_arc, lv_color_hex(theme.text), LV_PART_INDICATOR);

  lv_obj_set_style_bg_color(chart, lv_color_hex(theme.panel), 0);
  lv_obj_set_style_border_color(chart, lv_color_hex(theme.accent), 0);
  lv_obj_set_style_line_color(chart, lv_color_hex(theme.accent), LV_PART_ITEMS);
  lv_chart_set_series_color(chart, wave_series, lv_color_hex(theme.accent));

  lv_obj_set_style_bg_color(scan_sweep, lv_color_hex(theme.accent_alt), 0);
  if (active_mode == MODE_SCAN) {
    lv_obj_clear_flag(scan_sweep, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(scan_sweep, LV_OBJ_FLAG_HIDDEN);
  }

  for (uint8_t i = 0; i < 3; ++i) {
    lv_label_set_text(stat_name_label[i], STAT_NAMES[active_mode][i]);
    lv_obj_set_style_bg_color(stat_chip[i], lv_color_hex(theme.panel), 0);
    lv_obj_set_style_border_color(stat_chip[i], lv_color_hex(i == 1 ? theme.accent_alt : theme.accent), 0);
    lv_obj_set_style_shadow_color(stat_chip[i], lv_color_hex(theme.accent), 0);
    lv_obj_set_style_text_color(stat_name_label[i], lv_color_hex(theme.accent), 0);
    lv_obj_set_style_text_color(stat_value_label[i], lv_color_hex(theme.text), 0);
    lv_obj_set_style_bg_color(stat_bar[i], lv_color_hex(0x1B2730), LV_PART_MAIN);
    lv_obj_set_style_bg_color(stat_bar[i], lv_color_hex(i == 1 ? theme.accent_alt : theme.accent), LV_PART_INDICATOR);
  }

  for (uint8_t i = 0; i < 4; ++i) {
    lv_obj_set_style_bg_color(orbit_dot[i], lv_color_hex((i & 1) ? theme.accent_alt : theme.accent), 0);
  }
}

static void update_demo_values()
{
  const DemoTheme &theme = DEMO_THEMES[active_mode];
  char text[12];
  uint8_t values[3] = { 0, 0, 0 };

  const int32_t energy = clamp_percent(72 + static_cast<int32_t>(sinf(demo_phase * 0.70f) * 18.0f));
  const int32_t sync = clamp_percent(63 + static_cast<int32_t>(sinf(demo_phase * 0.53f + 1.1f) * 24.0f));
  const int32_t scan = clamp_percent(50 + static_cast<int32_t>(sinf(demo_phase * 1.30f) * 38.0f));
  const int32_t noise = clamp_percent(28 + static_cast<int32_t>(sinf(demo_phase * 0.91f + 2.0f) * 18.0f));
  const int32_t link_tx = clamp_percent(58 + static_cast<int32_t>(sinf(demo_phase * 1.05f + 0.2f) * 32.0f));
  const int32_t link_rx = clamp_percent(61 + static_cast<int32_t>(sinf(demo_phase * 0.82f + 2.7f) * 28.0f));
  const uint32_t heap_kb = ESP.getFreeHeap() / 1024U;
  const uint8_t heap_pct = clamp_percent(static_cast<int32_t>((heap_kb * 100U) / 170U));

  switch (active_mode) {
    case MODE_CORE:
      values[0] = energy;
      values[1] = sync;
      values[2] = heap_pct;
      break;
    case MODE_SCAN:
      values[0] = scan;
      values[1] = noise;
      values[2] = clamp_percent(100 - noise + scan / 4);
      break;
    case MODE_LINK:
      values[0] = link_tx;
      values[1] = link_rx;
      values[2] = heap_pct;
      break;
    default:
      break;
  }

  lv_arc_set_value(outer_arc, values[0]);
  lv_arc_set_value(inner_arc, values[1]);
  lv_arc_set_value(pulse_arc, clamp_percent(45 + static_cast<int32_t>(sinf(demo_phase * 1.65f) * 42.0f)));

  int16_t wave = 50;
  if (active_mode == MODE_CORE) {
    wave += static_cast<int16_t>(sinf(demo_phase * 2.2f) * 26.0f + sinf(demo_phase * 0.55f) * 10.0f);
  } else if (active_mode == MODE_SCAN) {
    wave += static_cast<int16_t>(sinf(demo_phase * 3.8f) * 18.0f + sinf(demo_phase * 0.72f) * 24.0f);
  } else {
    wave += static_cast<int16_t>(sinf(demo_phase * 1.8f) * 16.0f + sinf(demo_phase * 1.1f + 1.8f) * 18.0f);
  }
  lv_chart_set_next_value(chart, wave_series, clamp_percent(wave));

  for (uint8_t i = 0; i < 3; ++i) {
    lv_bar_set_value(stat_bar[i], values[i], LV_ANIM_ON);
  }

  if (++text_update_divider >= 3) {
    text_update_divider = 0;
    for (uint8_t i = 0; i < 3; ++i) {
      if ((active_mode == MODE_CORE || active_mode == MODE_LINK) && i == 2) {
        snprintf(text, sizeof(text), "%luK", static_cast<unsigned long>(heap_kb));
      } else {
        snprintf(text, sizeof(text), "%u%%", values[i]);
      }
      lv_label_set_text(stat_value_label[i], text);
    }
  }

  for (uint8_t i = 0; i < 4; ++i) {
    const float angle = demo_phase * 0.72f + (1.5708f * i);
    const int32_t x = 120 + static_cast<int32_t>(cosf(angle) * 105.0f);
    const int32_t y = 120 + static_cast<int32_t>(sinf(angle) * 105.0f);
    lv_obj_set_pos(orbit_dot[i], x - 3, y - 3);
  }

  lv_obj_set_style_shadow_color(core_img, lv_color_hex(theme.accent), 0);
}

static void demo_timer_cb(lv_timer_t *t)
{
  (void)t;
  demo_phase += 0.16f;
  update_demo_values();
}

// -------------------- Tap interaction --------------------
static void on_screen_tap(lv_event_t *e)
{
  (void)e;
  active_mode = static_cast<DemoMode>((static_cast<uint8_t>(active_mode) + 1) % MODE_COUNT);
  apply_mode();
}

// -------------------- Anim callbacks --------------------
static void anim_zoom_cb(void *var, int32_t v)
{
  lv_obj_set_style_transform_zoom((lv_obj_t*)var, (uint16_t)v, 0);
}

static void anim_rotate_cb(void *var, int32_t v)
{
  lv_obj_set_style_transform_angle((lv_obj_t*)var, (int16_t)v, 0);
}

static void anim_scan_y_cb(void *var, int32_t v)
{
  lv_obj_set_y((lv_obj_t*)var, v);
}

// -------------------- Build UI --------------------
static void create_stat_chip(lv_obj_t *parent, uint8_t index, lv_align_t align, int16_t x, int16_t y)
{
  stat_chip[index] = lv_obj_create(parent);
  lv_obj_set_size(stat_chip[index], 56, 37);
  lv_obj_align(stat_chip[index], align, x, y);
  style_status_chip(stat_chip[index]);

  stat_name_label[index] = create_centered_label(stat_chip[index], "--", &lv_font_montserrat_14);
  lv_obj_align(stat_name_label[index], LV_ALIGN_TOP_MID, 0, -1);

  stat_value_label[index] = create_centered_label(stat_chip[index], "--", &lv_font_montserrat_14);
  lv_obj_align(stat_value_label[index], LV_ALIGN_CENTER, 0, 5);

  stat_bar[index] = lv_bar_create(stat_chip[index]);
  clear_obj_interaction(stat_bar[index]);
  lv_obj_set_size(stat_bar[index], 44, 4);
  lv_obj_align(stat_bar[index], LV_ALIGN_BOTTOM_MID, 0, -1);
  lv_bar_set_range(stat_bar[index], 0, 100);
  lv_bar_set_value(stat_bar[index], 40, LV_ANIM_OFF);
  lv_obj_set_style_radius(stat_bar[index], 4, LV_PART_MAIN);
  lv_obj_set_style_radius(stat_bar[index], 4, LV_PART_INDICATOR);
}

static void create_ui()
{
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, on_screen_tap, LV_EVENT_CLICKED, NULL);

  outer_arc = lv_arc_create(scr);
  clear_obj_interaction(outer_arc);
  lv_obj_set_size(outer_arc, 232, 232);
  lv_obj_center(outer_arc);
  lv_arc_set_rotation(outer_arc, 270);
  lv_arc_set_bg_angles(outer_arc, 0, 360);
  lv_arc_set_range(outer_arc, 0, 100);
  lv_obj_remove_style(outer_arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(outer_arc, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_width(outer_arc, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(outer_arc, LV_OPA_30, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(outer_arc, LV_OPA_90, LV_PART_INDICATOR);

  inner_arc = lv_arc_create(scr);
  clear_obj_interaction(inner_arc);
  lv_obj_set_size(inner_arc, 188, 188);
  lv_obj_center(inner_arc);
  lv_arc_set_rotation(inner_arc, 90);
  lv_arc_set_bg_angles(inner_arc, 0, 360);
  lv_arc_set_range(inner_arc, 0, 100);
  lv_obj_remove_style(inner_arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(inner_arc, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_width(inner_arc, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(inner_arc, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(inner_arc, LV_OPA_80, LV_PART_INDICATOR);

  pulse_arc = lv_arc_create(scr);
  clear_obj_interaction(pulse_arc);
  lv_obj_set_size(pulse_arc, 146, 146);
  lv_obj_center(pulse_arc);
  lv_arc_set_rotation(pulse_arc, 205);
  lv_arc_set_bg_angles(pulse_arc, 20, 340);
  lv_arc_set_range(pulse_arc, 0, 100);
  lv_obj_remove_style(pulse_arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(pulse_arc, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(pulse_arc, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(pulse_arc, LV_OPA_10, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(pulse_arc, LV_OPA_60, LV_PART_INDICATOR);

  mode_badge = lv_obj_create(scr);
  clear_obj_interaction(mode_badge);
  lv_obj_set_size(mode_badge, 64, 21);
  lv_obj_align(mode_badge, LV_ALIGN_TOP_MID, 0, 7);
  lv_obj_set_style_radius(mode_badge, 7, 0);
  lv_obj_set_style_border_width(mode_badge, 0, 0);
  lv_obj_set_style_pad_all(mode_badge, 0, 0);
  lv_obj_set_style_shadow_width(mode_badge, 12, 0);
  lv_obj_set_style_shadow_opa(mode_badge, LV_OPA_30, 0);

  mode_label = create_centered_label(mode_badge, "CORE", &lv_font_montserrat_14);
  lv_obj_center(mode_label);

  title_label = create_centered_label(scr, "DGV-01", &lv_font_montserrat_16);
  lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 31);

  subtitle_label = create_centered_label(scr, "DIGI CORE ONLINE", &lv_font_montserrat_14);
  lv_obj_align(subtitle_label, LV_ALIGN_TOP_MID, 0, 50);

  static lv_img_dsc_t core_dsc;
  build_digicore_img(core_dsc);
  core_img = lv_img_create(scr);
  clear_obj_interaction(core_img);
  lv_img_set_src(core_img, &core_dsc);
  lv_obj_align(core_img, LV_ALIGN_CENTER, 0, -13);
  lv_obj_set_style_transform_pivot_x(core_img, 42, 0);
  lv_obj_set_style_transform_pivot_y(core_img, 42, 0);
  lv_obj_set_style_shadow_width(core_img, 18, 0);
  lv_obj_set_style_shadow_opa(core_img, LV_OPA_30, 0);
  lv_obj_set_style_shadow_spread(core_img, 1, 0);

  scan_sweep = lv_obj_create(scr);
  clear_obj_interaction(scan_sweep);
  lv_obj_set_size(scan_sweep, 112, 3);
  lv_obj_set_pos(scan_sweep, 64, 84);
  lv_obj_set_style_radius(scan_sweep, 3, 0);
  lv_obj_set_style_border_width(scan_sweep, 0, 0);
  lv_obj_set_style_bg_opa(scan_sweep, LV_OPA_80, 0);

  for (uint8_t i = 0; i < 4; ++i) {
    orbit_dot[i] = lv_obj_create(scr);
    clear_obj_interaction(orbit_dot[i]);
    lv_obj_set_size(orbit_dot[i], 7, 7);
    lv_obj_set_style_radius(orbit_dot[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(orbit_dot[i], 0, 0);
    lv_obj_set_style_bg_opa(orbit_dot[i], LV_OPA_90, 0);
  }

  create_stat_chip(scr, 0, LV_ALIGN_LEFT_MID, 13, -10);
  create_stat_chip(scr, 1, LV_ALIGN_RIGHT_MID, -13, -10);
  create_stat_chip(scr, 2, LV_ALIGN_BOTTOM_MID, 0, -49);

  chart = lv_chart_create(scr);
  clear_obj_interaction(chart);
  lv_obj_set_size(chart, 158, 34);
  lv_obj_align(chart, LV_ALIGN_BOTTOM_MID, 0, -15);
  lv_obj_set_style_radius(chart, 7, 0);
  lv_obj_set_style_bg_opa(chart, LV_OPA_50, 0);
  lv_obj_set_style_border_width(chart, 1, 0);
  lv_obj_set_style_pad_all(chart, 3, 0);
  lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
  lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_point_count(chart, 46);
  lv_chart_set_div_line_count(chart, 0, 0);
  wave_series = lv_chart_add_series(chart, lv_color_white(), LV_CHART_AXIS_PRIMARY_Y);
  for (uint8_t i = 0; i < 46; ++i) lv_chart_set_next_value(chart, wave_series, 50);

  hint_label = create_centered_label(scr, "tap: field scan", &lv_font_montserrat_14);
  lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, 3);
  lv_obj_set_style_text_opa(hint_label, LV_OPA_70, 0);

  apply_mode();
  update_demo_values();

  lv_anim_t zoom;
  lv_anim_init(&zoom);
  lv_anim_set_var(&zoom, core_img);
  lv_anim_set_values(&zoom, 250, 282);
  lv_anim_set_time(&zoom, 760);
  lv_anim_set_playback_time(&zoom, 760);
  lv_anim_set_repeat_count(&zoom, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&zoom, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&zoom, anim_zoom_cb);
  lv_anim_start(&zoom);

  lv_anim_t tilt;
  lv_anim_init(&tilt);
  lv_anim_set_var(&tilt, core_img);
  lv_anim_set_values(&tilt, -25, 25);
  lv_anim_set_time(&tilt, 1400);
  lv_anim_set_playback_time(&tilt, 1400);
  lv_anim_set_repeat_count(&tilt, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&tilt, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&tilt, anim_rotate_cb);
  lv_anim_start(&tilt);

  lv_anim_t scan;
  lv_anim_init(&scan);
  lv_anim_set_var(&scan, scan_sweep);
  lv_anim_set_values(&scan, 82, 137);
  lv_anim_set_time(&scan, 900);
  lv_anim_set_playback_time(&scan, 180);
  lv_anim_set_repeat_count(&scan, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&scan, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&scan, anim_scan_y_cb);
  lv_anim_start(&scan);

  lv_timer_create(demo_timer_cb, 120, NULL);
}

void setup()
{
  // Backlight
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  Serial.begin(115200);
  delay(50);

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  touch.begin();

  // TFT
  tft.init();
  tft.setColorDepth(16);
  tft.initDMA();

  // LVGL init
  lv_init();

  // LVGL draw buffers
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, screenWidth * BUF_LINES);

  // Display driver
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Input driver
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  // LVGL tick via esp_timer (1ms)
  esp_timer_create_args_t tick_args = {
    .callback = &lv_tick_task,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "lvgl_tick"
  };
  esp_timer_create(&tick_args, &lvgl_tick_timer);
  esp_timer_start_periodic(lvgl_tick_timer, 1000);

  // UI
  create_ui();

  Serial.println("UI ready ✅");
}

void loop()
{
  lv_timer_handler();   // procesa animaciones, input, render
  delay(3);             // pequeño respiro al CPU (ajusta si quieres)
}
