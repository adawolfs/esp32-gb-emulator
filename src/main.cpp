#include <Arduino.h>

#include "emulator_runtime.h"

static bool emulator_ready = false;

void setup() {
  Serial.begin(115200);
  delay(150);

  emulator_ready = emulator_init();
  if (emulator_ready) Serial.println("GB ready");
}

void loop() {
  if (!emulator_ready) {
    delay(1000);
    return;
  }
  emulator_run_frame();
}
