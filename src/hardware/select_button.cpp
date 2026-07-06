#include "hardware/select_button.h"

#include <Arduino.h>

#include "config.h"

namespace {

bool s_last_down = false;
unsigned long s_down_ms = 0;
bool s_tap_pending = false;

}  // namespace

void selectButtonInit() { pinMode(config::kSelectPin, INPUT_PULLUP); }

bool selectButtonConsumeTap() {
  const bool down = digitalRead(config::kSelectPin) == LOW;
  const unsigned long now = millis();

  if (down && !s_last_down) {
    s_down_ms = now;
  } else if (!down && s_last_down && now - s_down_ms >= config::kSelectTapMinMs) {
    s_tap_pending = true;
  }
  s_last_down = down;

  if (!s_tap_pending) {
    return false;
  }
  s_tap_pending = false;
  return true;
}
