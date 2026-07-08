#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

/** LovyanGFX device: ILI9341 (TJCTM24024-SPI) + XPT2046 touch. Pin values come from config.h. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_ILI9341 _panel;
  lgfx::Touch_XPT2046 _touch;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.pin_sclk = static_cast<int>(config::kDisplayPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kDisplayPinMosi);
      cfg.pin_miso = static_cast<int>(config::kTouchPinMiso);
      cfg.pin_dc = static_cast<int>(config::kDisplayPinDc);
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = static_cast<int>(config::kDisplayPinCs);
      cfg.pin_rst = static_cast<int>(config::kDisplayPinRst);
      cfg.panel_width = config::kDisplayWidth;
      cfg.panel_height = config::kDisplayHeight;
      cfg.invert = config::kDisplayInvert;
      cfg.rgb_order = config::kDisplayRgbOrder;
      _panel.config(cfg);
    }
    {
      auto cfg = _touch.config();
      cfg.bus_shared = true;
      cfg.spi_host = SPI2_HOST;
      cfg.pin_sclk = static_cast<int>(config::kDisplayPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kDisplayPinMosi);
      cfg.pin_miso = static_cast<int>(config::kTouchPinMiso);
      cfg.pin_cs = static_cast<int>(config::kTouchPinCs);
      // Not wiring pin_int: LovyanGFX's XPT2046 driver treats a floating/high
      // IRQ line as "never touched" and skips the SPI read entirely unless
      // it's pulled up, which most breakouts don't guarantee. Detecting
      // touches from the SPI pressure reading alone is self-contained and
      // doesn't depend on that pull-up.
      cfg.x_min = config::kTouchXMin;
      cfg.x_max = config::kTouchXMax;
      cfg.y_min = config::kTouchYMin;
      cfg.y_max = config::kTouchYMax;
      // Touch chip's raw Y axis runs opposite to the panel's; measured
      // empirically (top touches read near max Y, bottom near min Y).
      // offset_rotation=4 vertically flips Y without swapping/mirroring X.
      cfg.offset_rotation = 4;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
