#include "touch_input.h"

#include <Arduino.h>

#include "CST816D.h"
#include "board_config.h"
#include "interrupt.h"

static CST816D touch(board::PIN_TOUCH_SDA, board::PIN_TOUCH_SCL,
                     board::PIN_TOUCH_RST, board::PIN_TOUCH_INT);

static constexpr uint32_t WEB_INPUT_TIMEOUT_MS = board::WEB_INPUT_TIMEOUT_MS;
static constexpr uint32_t WEB_MIN_PRESS_MS = board::WEB_MIN_PRESS_MS;

static unsigned int touch_buttons;
static unsigned int touch_directions;
static unsigned int web_buttons;
static unsigned int web_directions;
static unsigned int previous_buttons;
static unsigned int previous_directions;
static unsigned int last_logged_touch_buttons;
static unsigned int last_logged_touch_directions;
static unsigned int last_logged_web_buttons;
static unsigned int last_logged_web_directions;
static uint32_t web_button_pressed_at[4];
static uint32_t web_direction_pressed_at[4];
static uint32_t web_button_release_due[4];
static uint32_t web_direction_release_due[4];
static uint32_t last_web_input_ms;

static void clear_touch_state() {
  touch_buttons = 0;
  touch_directions = 0;
}

static bool matches(const char *value, const char *expected) {
  if (!value || !expected) return false;
  while (*value && *expected) {
    if (*value++ != *expected++) return false;
  }
  return *value == '\0' && *expected == '\0';
}

static void log_touch_state(uint16_t tx, uint16_t ty, uint8_t gesture) {
  if (!board::DEBUG_TOUCH_INPUT) return;
  if (touch_buttons == last_logged_touch_buttons &&
      touch_directions == last_logged_touch_directions) {
    return;
  }

  printf("[touch] x=%u y=%u gesture=%u buttons=0x%X directions=0x%X\n", tx, ty,
         gesture, touch_buttons, touch_directions);
  last_logged_touch_buttons = touch_buttons;
  last_logged_touch_directions = touch_directions;
}

static void log_web_state(const char *source, const char *control, bool pressed,
                          uint32_t now_ms) {
  if (!board::DEBUG_WEB_INPUT) return;
  if (web_buttons == last_logged_web_buttons &&
      web_directions == last_logged_web_directions) {
    return;
  }

  printf(
      "[web input] source=%s control=%s pressed=%d now_ms=%lu buttons=0x%X directions=0x%X\n",
      source ? source : "unknown", control ? control : "(none)", pressed ? 1 : 0,
      static_cast<unsigned long>(now_ms), web_buttons, web_directions);
  last_logged_web_buttons = web_buttons;
  last_logged_web_directions = web_directions;
}

static void set_web_bit(unsigned int &target, uint32_t pressed_at[4],
                        uint32_t release_due[4], unsigned int bit,
                        bool pressed, uint32_t now_ms) {
  const unsigned int mask = 1u << bit;

  if (pressed) {
    if (!(target & mask)) pressed_at[bit] = now_ms;
    release_due[bit] = 0;
    target |= mask;
    return;
  }

  const uint32_t earliest_release = pressed_at[bit] + WEB_MIN_PRESS_MS;
  release_due[bit] = now_ms < earliest_release ? earliest_release : now_ms;
}

static void apply_due_releases(unsigned int &target, uint32_t release_due[4],
                               uint32_t now_ms) {
  for (unsigned int bit = 0; bit < 4; ++bit) {
    if (release_due[bit] && now_ms >= release_due[bit]) {
      target &= ~(1u << bit);
      release_due[bit] = 0;
    }
  }
}

static bool has_pending_release(const uint32_t release_due[4]) {
  for (unsigned int bit = 0; bit < 4; ++bit) {
    if (release_due[bit] != 0) return true;
  }
  return false;
}

static bool web_input_timeout_expired(uint32_t now_ms) {
  if (WEB_INPUT_TIMEOUT_MS == 0) return false;
  if (last_web_input_ms == 0) return false;

  if (now_ms < last_web_input_ms) {
    if (board::DEBUG_WEB_INPUT) {
      printf("[web input] clock regression now_ms=%lu last_input_ms=%lu\n",
             static_cast<unsigned long>(now_ms),
             static_cast<unsigned long>(last_web_input_ms));
    }
    return false;
  }

  return now_ms - last_web_input_ms > WEB_INPUT_TIMEOUT_MS;
}

static void request_joypad_interrupt_on_new_press() {
  const unsigned int current_buttons = touch_input_buttons();
  const unsigned int current_directions = touch_input_directions();
  const bool button_pressed = (current_buttons & ~previous_buttons) != 0;
  const bool direction_pressed = (current_directions & ~previous_directions) != 0;

  if (button_pressed || direction_pressed) {
    interrupt(INTR_JOYPAD);
  }
  previous_buttons = current_buttons;
  previous_directions = current_directions;
}

void touch_input_init(void) {
  clear_touch_state();
  touch_input_clear_web_controls();
  previous_buttons = 0;
  previous_directions = 0;
  last_logged_touch_buttons = 0;
  last_logged_touch_directions = 0;
  last_logged_web_buttons = 0;
  last_logged_web_directions = 0;
  touch.begin();
  if (board::DEBUG_TOUCH_INPUT) {
    printf("[touch] initialized\n");
  }
}

void touch_input_update(void) {
  uint16_t tx = 0;
  uint16_t ty = 0;
  uint8_t gesture = 0;
  const unsigned int previous_touch_buttons = touch_buttons;
  const unsigned int previous_touch_directions = touch_directions;

  clear_touch_state();
  if (!touch.getTouch(&tx, &ty, &gesture)) {
    if (previous_touch_buttons || previous_touch_directions) {
      if (board::DEBUG_TOUCH_INPUT) {
        printf("[touch] released buttons=0x%X directions=0x%X\n",
               previous_touch_buttons, previous_touch_directions);
      }
      last_logged_touch_buttons = 0;
      last_logged_touch_directions = 0;
    }
    return;
  }

  if (tx < 80) {
    if (ty < 60) {
      touch_buttons |= (1u << 2);
    } else if (ty < 160) {
      touch_directions |= (1u << 1);
    } else {
      touch_directions |= (1u << 3);
    }
  } else if (tx < 160) {
    if (ty < 60) {
      touch_directions |= (1u << 2);
    } else if (ty >= 160) {
      touch_directions |= (1u << 3);
    }
  } else {
    if (ty < 60) {
      touch_buttons |= (1u << 3);
    } else if (tx > 200 && ty < 160) {
      touch_directions |= (1u << 0);
    } else if (ty < 160) {
      touch_buttons |= (1u << 0);
    } else {
      touch_buttons |= (1u << 1);
    }
  }

  log_touch_state(tx, ty, gesture);
  request_joypad_interrupt_on_new_press();
}

void touch_input_maintain(uint32_t now_ms) {
  const bool web_active = (web_buttons | web_directions) != 0;
  const bool pending_release =
      has_pending_release(web_button_release_due) ||
      has_pending_release(web_direction_release_due);

  if (!web_active && !pending_release) return;

  if ((web_buttons || web_directions) && web_input_timeout_expired(now_ms)) {
    if (board::DEBUG_WEB_INPUT) {
      printf(
          "[web input] timeout now_ms=%lu last_input_ms=%lu buttons=0x%X directions=0x%X\n",
          static_cast<unsigned long>(now_ms),
          static_cast<unsigned long>(last_web_input_ms), web_buttons,
          web_directions);
    }
    touch_input_clear_web_controls();
  }

  apply_due_releases(web_buttons, web_button_release_due, now_ms);
  apply_due_releases(web_directions, web_direction_release_due, now_ms);
  log_web_state("maintain", "(pending release)", false, now_ms);
  request_joypad_interrupt_on_new_press();
}

void touch_input_set_web_control(const char *control, bool pressed,
                                 uint32_t now_ms) {
  bool matched = true;

  if (matches(control, "a")) {
    set_web_bit(web_buttons, web_button_pressed_at, web_button_release_due, 0,
                pressed, now_ms);
  } else if (matches(control, "b")) {
    set_web_bit(web_buttons, web_button_pressed_at, web_button_release_due, 1,
                pressed, now_ms);
  } else if (matches(control, "select")) {
    set_web_bit(web_buttons, web_button_pressed_at, web_button_release_due, 2,
                pressed, now_ms);
  } else if (matches(control, "start")) {
    set_web_bit(web_buttons, web_button_pressed_at, web_button_release_due, 3,
                pressed, now_ms);
  } else if (matches(control, "right")) {
    set_web_bit(web_directions, web_direction_pressed_at,
                web_direction_release_due, 0, pressed, now_ms);
  } else if (matches(control, "left")) {
    set_web_bit(web_directions, web_direction_pressed_at,
                web_direction_release_due, 1, pressed, now_ms);
  } else if (matches(control, "up")) {
    set_web_bit(web_directions, web_direction_pressed_at,
                web_direction_release_due, 2, pressed, now_ms);
  } else if (matches(control, "down")) {
    set_web_bit(web_directions, web_direction_pressed_at,
                web_direction_release_due, 3, pressed, now_ms);
  } else {
    matched = false;
  }

  if (!matched && board::DEBUG_WEB_INPUT) {
    printf("[web input] unknown control=%s pressed=%d now_ms=%lu\n",
           control ? control : "(null)", pressed ? 1 : 0,
           static_cast<unsigned long>(now_ms));
  }

  last_web_input_ms = now_ms;
  log_web_state("socket", control, pressed, now_ms);
  request_joypad_interrupt_on_new_press();
}

void touch_input_clear_web_controls(void) {
  if ((web_buttons || web_directions) && board::DEBUG_WEB_INPUT) {
    printf("[web input] cleared buttons=0x%X directions=0x%X\n", web_buttons,
           web_directions);
  }
  web_buttons = 0;
  web_directions = 0;
  for (unsigned int bit = 0; bit < 4; ++bit) {
    web_button_pressed_at[bit] = 0;
    web_direction_pressed_at[bit] = 0;
    web_button_release_due[bit] = 0;
    web_direction_release_due[bit] = 0;
  }
  last_web_input_ms = 0;
  last_logged_web_buttons = 0;
  last_logged_web_directions = 0;
}

unsigned int touch_input_buttons(void) { return touch_buttons | web_buttons; }

unsigned int touch_input_directions(void) {
  return touch_directions | web_directions;
}

unsigned int touch_input_web_buttons(void) { return web_buttons; }

unsigned int touch_input_web_directions(void) { return web_directions; }
