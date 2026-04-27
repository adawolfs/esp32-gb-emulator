#include "touch_input.h"

#include <Arduino.h>

#include "CST816D.h"
#include "board_config.h"

static CST816D touch(board::PIN_TOUCH_SDA, board::PIN_TOUCH_SCL,
                     board::PIN_TOUCH_RST, board::PIN_TOUCH_INT);

static unsigned int buttons;
static unsigned int directions;

static void clear_state() {
  buttons = 0;
  directions = 0;
}

void touch_input_init(void) {
  clear_state();
  touch.begin();
}

void touch_input_update(void) {
  uint16_t tx = 0;
  uint16_t ty = 0;
  uint8_t gesture = 0;

  clear_state();
  if (!touch.getTouch(&tx, &ty, &gesture)) return;

  if (tx < 80) {
    if (ty < 60) {
      buttons |= (1u << 2);
    } else if (ty < 160) {
      directions |= (1u << 1);
    } else {
      directions |= (1u << 3);
    }
  } else if (tx < 160) {
    if (ty < 60) {
      directions |= (1u << 2);
    } else if (ty >= 160) {
      directions |= (1u << 3);
    }
  } else {
    if (ty < 60) {
      buttons |= (1u << 3);
    } else if (tx > 200 && ty < 160) {
      directions |= (1u << 0);
    } else if (ty < 160) {
      buttons |= (1u << 0);
    } else {
      buttons |= (1u << 1);
    }
  }
}

unsigned int touch_input_buttons(void) { return buttons; }

unsigned int touch_input_directions(void) { return directions; }
