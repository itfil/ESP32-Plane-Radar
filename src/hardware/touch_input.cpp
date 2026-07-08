#include "hardware/touch_input.h"

#include <Arduino.h>

#include "config.h"
#include "hardware/display.h"
#include "ui/radar_theme.h"

namespace {

bool s_was_touched = false;
unsigned long s_touch_down_ms = 0;
int32_t s_touch_down_y = 0;
bool s_range_tap_pending = false;
bool s_select_tap_pending = false;

}  // namespace

void touchInit() {
  // No setup needed here: the XPT2046 is attached via _panel.setTouch() in
  // lgfx_config.hpp, so tft.init() (already called in displayInit()) brings
  // the touch controller up along with the panel.
}

void touchPoll() {
  int32_t x = 0;
  int32_t y = 0;
  const bool touched = tft.getTouch(&x, &y) > 0;
  const unsigned long now = millis();

  if (touched != s_was_touched) {
    Serial.printf("touch: %s at (%ld, %ld)\n", touched ? "down" : "up", (long)x, (long)y);
  }

  if (touched && !s_was_touched) {
    s_touch_down_ms = now;
    s_touch_down_y = y;
  } else if (!touched && s_was_touched &&
             now - s_touch_down_ms >= config::kTouchTapMinMs) {
    if (s_touch_down_y < ui::radar::kSize) {
      s_range_tap_pending = true;
      Serial.printf("touch: tap at y=%ld -> RANGE (radar zone, y < %d)\n",
                    (long)s_touch_down_y, ui::radar::kSize);
    } else {
      s_select_tap_pending = true;
      Serial.printf("touch: tap at y=%ld -> SELECT (panel zone, y >= %d)\n",
                    (long)s_touch_down_y, ui::radar::kSize);
    }
  }
  s_was_touched = touched;
}

bool touchConsumeRangeTap() {
  if (!s_range_tap_pending) {
    return false;
  }
  s_range_tap_pending = false;
  return true;
}

bool touchConsumeSelectTap() {
  if (!s_select_tap_pending) {
    return false;
  }
  s_select_tap_pending = false;
  return true;
}
