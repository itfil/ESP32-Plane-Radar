#include "hardware/display.h"

#include "config.h"
#include "hardware/display_font.h"

LGFX tft;

void displayInit() {
  tft.init();
  tft.setRotation(config::kDisplayRotation);
  tft.setTextWrap(false);
  displayFontInit();
}
