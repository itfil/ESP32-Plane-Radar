#include "hardware/display.h"

#include "hardware/display_font.h"

LGFX tft;

void displayInit() {
  tft.init();
  tft.setRotation(0);
  tft.setTextWrap(false);
  displayFontInit();
}
