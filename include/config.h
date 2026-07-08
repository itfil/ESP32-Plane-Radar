#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://plane-radar.local */
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- BOOT button (ESP32-C3 Super Mini, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_9;
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: TJCTM24024-SPI 2.4" ILI9341 240×320 (SPI) ---
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;  // display SDI(MOSI)
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;  // display SCK
// LED (backlight) is wired directly to 3V3 — no GPIO/PWM control.

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 320;

// --- Touch: XPT2046 on the TJCTM24024-SPI panel (shares the display's SCLK/MOSI) ---
constexpr gpio_num_t kTouchPinCs = GPIO_NUM_7;
constexpr gpio_num_t kTouchPinMiso = GPIO_NUM_5;  // T_DO; also enables readback on the shared bus
constexpr gpio_num_t kTouchPinIrq = GPIO_NUM_6;
/** Raw ADC calibration; XPT2046 defaults — tune against the actual panel if touches misregister. */
constexpr uint16_t kTouchXMin = 300;
constexpr uint16_t kTouchXMax = 3900;
constexpr uint16_t kTouchYMin = 400;
constexpr uint16_t kTouchYMax = 3900;
/** Ignore touch taps shorter than this (debounce). */
constexpr unsigned long kTouchTapMinMs = 40UL;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// ILI9341 modules typically need neither invert nor BGR swap; adjust if colors look wrong
constexpr bool kDisplayInvert = false;
constexpr bool kDisplayRgbOrder = false;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 46.6390;
constexpr double kDefaultRadarLon = 7.0569;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 5000;
/** Legacy scale unused — fetch uses radar::fetchRadiusKm() to screen edge. */
constexpr float kAdsbFetchRadiusScale = 1.0f;
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;
/** Medium grey — info-panel text once the selected aircraft drops off the feed. */
constexpr uint16_t kColorGreyText = 0x8410;

}  // namespace config
