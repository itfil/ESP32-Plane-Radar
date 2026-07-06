#include "hardware/select_button.h"

#include <Arduino.h>

#include "config.h"

namespace {

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_tap_pending = false;
volatile bool s_is_down = false;
volatile unsigned long s_down_ms = 0;
bool s_interrupt_attached = false;

// Interrupt-latched (not polled) so a tap during a blocking network call — the
// select-triggered route/aircraft lookups, or the periodic ADS-B fetch — is
// never silently dropped; it's just handled once loop() gets back around.
void IRAM_ATTR onSelectButtonIsr() {
  const bool down = digitalRead(config::kSelectPin) == LOW;
  const unsigned long now = millis();
  portENTER_CRITICAL_ISR(&s_mux);
  if (down) {
    s_is_down = true;
    s_down_ms = now;
  } else if (s_is_down) {
    if (now - s_down_ms >= config::kSelectTapMinMs) {
      s_tap_pending = true;
    }
    s_is_down = false;
  }
  portEXIT_CRITICAL_ISR(&s_mux);
}

}  // namespace

void selectButtonInit() {
  pinMode(config::kSelectPin, INPUT_PULLUP);
  if (s_interrupt_attached) {
    return;
  }
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kSelectPin)),
                  onSelectButtonIsr, CHANGE);
  s_interrupt_attached = true;
}

bool selectButtonConsumeTap() {
  portENTER_CRITICAL(&s_mux);
  const bool tap = s_tap_pending;
  if (tap) {
    s_tap_pending = false;
  }
  portEXIT_CRITICAL(&s_mux);
  return tap;
}
