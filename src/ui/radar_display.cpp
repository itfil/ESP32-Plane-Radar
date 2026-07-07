#include "ui/radar_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/runway_overlay.h"

namespace ui {
namespace radar {

uint16_t kColorBackground = 0x0000;
uint16_t kColorGrid = 0x0320;
uint16_t kColorLabel = 0xFFFF;
uint16_t kColorCenter = 0xFFFF;
uint16_t kColorAircraft = 0x001F;
uint16_t kColorTrackVector = 0xFFFF;
uint16_t kColorTagType = 0x5DFF;
uint16_t kColorTagAltitude = 0xFFE0;
uint16_t kColorRunway = 0x4D5F;
uint16_t kColorRunwayLabel = 0x7DFF;

}  // namespace radar

namespace {

bool s_label_metrics_ready = false;
bool s_cardinal_use_vlw = false;
bool s_scale_use_vlw = false;
bool s_outer_range_use_vlw = false;
float s_cardinal_vlw_size = 0.56f;
float s_scale_vlw_size = 0.50f;
float s_tag_vlw_size = 0.56f;
float s_outer_range_vlw_size = 0.7f;
const lgfx::GFXfont* s_cardinal_gfx = &fonts::FreeSansBold12pt7b;
const lgfx::GFXfont* s_scale_gfx = &fonts::FreeSansBold9pt7b;
const lgfx::GFXfont* s_tag_gfx = &fonts::FreeSansBold12pt7b;
const lgfx::GFXfont* s_outer_range_gfx = &fonts::FreeSansBold12pt7b;

bool s_tag_label_metrics_ready = false;
bool s_tag_use_vlw = false;

bool s_panel_label_metrics_ready = false;
bool s_panel_use_vlw = false;
float s_panel_vlw_size = 0.5f;
const lgfx::GFXfont* s_panel_gfx = &fonts::FreeSans9pt7b;

int s_scale_label_max_w = 0;
int s_scale_label_h = 0;

lgfx::LovyanGFX* s_draw = &tft;
LGFX_Sprite s_frame(&tft);
bool s_frame_ready = false;

// Info-panel selection; declared here (not near drawSelectionPanel further
// down) so drawAircraft() can highlight the selected symbol on the radar.
bool s_has_selection = false;
services::adsb::Aircraft s_selected;
/** False once the selected aircraft drops out of the ADS-B feed; panel greys out and shows GONE. */
bool s_selected_present = true;
/** millis() timestamp of the moment it was lost; drives the "GONE Xs" readout. */
unsigned long s_lost_since_ms = 0;

class DrawScope {
 public:
  explicit DrawScope(lgfx::LovyanGFX& gfx) : prev_(s_draw) { s_draw = &gfx; }
  ~DrawScope() { s_draw = prev_; }

 private:
  lgfx::LovyanGFX* prev_;
};

int absDiff(int a, int b) { return std::abs(a - b); }

int measureGfxHeight(const lgfx::GFXfont& font) {
  tft.setFont(&font);
  tft.setTextSize(1);
  return tft.fontHeight();
}

int measureVlwHeight(float size) {
  tft.setTextSize(size);
  return tft.fontHeight();
}

float findVlwSizeForHeight(int target_px) {
  float lo = 0.25f;
  float hi = 1.2f;
  for (int i = 0; i < 16; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (measureVlwHeight(mid) < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void applyScaleStyle();
void applyOuterRangeStyle();

const lgfx::GFXfont* pickGfxFontClosest(
    int target_px, const lgfx::GFXfont* const* candidates, size_t count) {
  const lgfx::GFXfont* best = candidates[0];
  int best_diff = absDiff(measureGfxHeight(*best), target_px);

  for (size_t i = 1; i < count; ++i) {
    const int diff = absDiff(measureGfxHeight(*candidates[i]), target_px);
    if (diff < best_diff) {
      best_diff = diff;
      best = candidates[i];
    }
  }
  return best;
}

void initLabelMetrics() {
  if (s_label_metrics_ready) {
    return;
  }

  const int cardinal_target = radar::kCardinalLabelHeightPx;

  if (displayFontIsSmooth()) {
    s_cardinal_use_vlw = true;
    s_cardinal_vlw_size = findVlwSizeForHeight(cardinal_target);
    const int cardinal_h = measureVlwHeight(s_cardinal_vlw_size);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    s_scale_use_vlw = true;
    s_scale_vlw_size = findVlwSizeForHeight(scale_target);
  } else {
    const lgfx::GFXfont* cardinal_candidates[] = {&fonts::FreeSansBold12pt7b,
                                                  &fonts::FreeSansBold9pt7b};
    s_cardinal_gfx =
        pickGfxFontClosest(cardinal_target, cardinal_candidates, 2);
    s_cardinal_use_vlw = false;

    const int cardinal_h = measureGfxHeight(*s_cardinal_gfx);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    const lgfx::GFXfont* scale_candidates[] = {&fonts::FreeSansBold9pt7b,
                                               &fonts::FreeSansBold12pt7b};
    s_scale_gfx = pickGfxFontClosest(scale_target, scale_candidates, 2);
    s_scale_use_vlw = false;
  }

  const int outer_range_target = radar::kOuterRangeLabelHeightPx;
  if (displayFontIsSmooth()) {
    s_outer_range_use_vlw = true;
    s_outer_range_vlw_size = findVlwSizeForHeight(outer_range_target);
  } else {
    const lgfx::GFXfont* outer_range_candidates[] = {&fonts::FreeSansBold18pt7b,
                                                     &fonts::FreeSansBold12pt7b};
    s_outer_range_gfx =
        pickGfxFontClosest(outer_range_target, outer_range_candidates, 2);
    s_outer_range_use_vlw = false;
  }

  applyScaleStyle();
  s_scale_label_h = tft.fontHeight();
  s_scale_label_max_w = 0;
  char label[12];
  for (size_t i = 0; i < radar::kRangePresetCount; ++i) {
    for (bool miles : {false, true}) {
      radar::formatRing3Label(label, sizeof(label), radar::kRangePresets[i].ring3_km,
                              miles);
      const int w = tft.textWidth(label);
      if (w > s_scale_label_max_w) {
        s_scale_label_max_w = w;
      }
    }
  }

  s_label_metrics_ready = true;
}

void initTagLabelMetrics() {
  if (s_tag_label_metrics_ready) {
    return;
  }

  const int target = radar::kAircraftTagLabelHeightPx;
  if (displayFontIsSmooth()) {
    s_tag_use_vlw = true;
    s_tag_vlw_size = findVlwSizeForHeight(target);
  } else {
    const lgfx::GFXfont* tag_candidates[] = {&fonts::FreeSansBold12pt7b,
                                               &fonts::FreeSansBold9pt7b};
    s_tag_gfx = pickGfxFontClosest(target, tag_candidates, 2);
    s_tag_use_vlw = false;
  }

  s_tag_label_metrics_ready = true;
}

void initPanelLabelMetrics() {
  if (s_panel_label_metrics_ready) {
    return;
  }

  const int target = radar::kInfoPanelLineHeightPx;
  if (displayFontIsSmooth()) {
    s_panel_use_vlw = true;
    s_panel_vlw_size = findVlwSizeForHeight(target);
  } else {
    s_panel_gfx = &fonts::FreeSans9pt7b;
    s_panel_use_vlw = false;
  }

  s_panel_label_metrics_ready = true;
}

void initPalette() {
  radar::kColorBackground = tft.color565(radar::kBgR, radar::kBgG, radar::kBgB);
  radar::kColorGrid = tft.color565(radar::kGridR, radar::kGridG, radar::kGridB);
  radar::kColorLabel = tft.color565(255, 255, 255);
  radar::kColorCenter = tft.color565(255, 255, 255);
  // GC9A01 BGR panel: swap R/B in color565 so logical red renders red on screen.
  if (config::kDisplayRgbOrder) {
    radar::kColorAircraft =
        tft.color565(radar::kAircraftB, radar::kAircraftG, radar::kAircraftR);
  } else {
    radar::kColorAircraft =
        tft.color565(radar::kAircraftR, radar::kAircraftG, radar::kAircraftB);
  }
  radar::kColorTrackVector =
      tft.color565(radar::kTrackR, radar::kTrackG, radar::kTrackB);
  radar::kColorTagType =
      tft.color565(radar::kTagTypeR, radar::kTagTypeG, radar::kTagTypeB);
  radar::kColorTagAltitude =
      tft.color565(radar::kTagAltR, radar::kTagAltG, radar::kTagAltB);
  radar::kColorRunway =
      tft.color565(radar::kRunwayR, radar::kRunwayG, radar::kRunwayB);
  radar::kColorRunwayLabel = tft.color565(radar::kRunwayLabelR, radar::kRunwayLabelG,
                                          radar::kRunwayLabelB);
}

constexpr float kKmPerDeg = 111.0f;

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km) {
  *dx_km =
      static_cast<float>(lon - services::location::lon()) * kKmPerDeg;
  *dy_km =
      static_cast<float>(lat - services::location::lat()) * kKmPerDeg;
  *dist_km = sqrtf((*dx_km) * (*dx_km) + (*dy_km) * (*dy_km));
}

float innerRingMaxKm() {
  const float outer_km = radar::rangeCurrent().outer_km;
  return outer_km * (static_cast<float>(radar::kGridOuterRadius -
                                       radar::kAircraftInsideRingInsetPx) /
                     static_cast<float>(radar::kGridOuterRadius));
}

/** Flat lat/lon as x/y: 1° ≈ 111 km, north = screen up. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = radar::rangeCurrent().outer_km;
  const float px_per_km = static_cast<float>(radar::kGridOuterRadius) / outer_km;

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

  *out_x = radar::kCenterX + static_cast<int>(lroundf(dx_km * px_per_km));
  *out_y = radar::kCenterY - static_cast<int>(lroundf(dy_km * px_per_km));
}

bool isInsideOuterRingKm(float dist_km) { return dist_km <= innerRingMaxKm(); }

int distSqFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
}

bool isInsideOuterRing(int x, int y) {
  const int max_r = radar::kGridOuterRadius - radar::kAircraftInsideRingInsetPx;
  return distSqFromCenter(x, y) <= max_r * max_r;
}

/** Rim dot from true bearing; always on screen edge (even if target is 50+ km away). */
bool beyondRingEdgeDotFromLatLon(float lat, float lon, int* out_x, int* out_y) {
  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);
  if (dist_km < 0.01f) {
    return false;
  }
  if (isInsideOuterRingKm(dist_km)) {
    return false;
  }

  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int rim_r = radar::kCenterX - radar::kBeyondRingScreenMarginPx;
  const float angle_rad = atan2f(dx_km, dy_km);

  *out_x = cx + static_cast<int>(lroundf(sinf(angle_rad) * rim_r));
  *out_y = cy - static_cast<int>(lroundf(cosf(angle_rad) * rim_r));
  return true;
}

void drawBeyondRingDot(int x, int y) {
  s_draw->fillSmoothCircle(x, y, radar::kBeyondRingDotRadiusPx,
                           radar::kColorAircraft);
}

void clipPointToOuterRing(int x0, int y0, int* x1, int* y1) {
  const int max_r = radar::kGridOuterRadius;
  const int max_r_sq = max_r * max_r;
  if (distSqFromCenter(*x1, *y1) <= max_r_sq) {
    return;
  }

  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  float t = 1.0f;
  for (int step = 0; step < 20; ++step) {
    const int px = x0 + static_cast<int>(lroundf(dx * t));
    const int py = y0 + static_cast<int>(lroundf(dy * t));
    if (distSqFromCenter(px, py) <= max_r_sq) {
      *x1 = px;
      *y1 = py;
      return;
    }
    t -= 0.05f;
    if (t <= 0.0f) {
      *x1 = x0;
      *y1 = y0;
      return;
    }
  }
}

int speedLineLengthPx(float gs_knots) {
  if (gs_knots <= 0.0f) {
    return 0;
  }

  // Fixed screen scale: 60 s horizon at gs, not tied to current range zoom.
  constexpr float kKmPerKnotPerHorizon =
      1.852f * radar::kAircraftTrackHorizonSec / 3600.0f;
  const float px =
      gs_knots * kKmPerKnotPerHorizon * radar::kGridOuterRadius /
      radar::kAircraftTrackRefOuterKm * radar::kAircraftTrackLengthScale;

  const int len = static_cast<int>(px + 0.5f);
  if (len < radar::kAircraftSpeedLineMinPx) {
    return radar::kAircraftSpeedLineMinPx;
  }
  return len;
}

void noseTip(int cx, int cy, float heading_deg, int* tip_x, int* tip_y) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  *tip_x = cx + static_cast<int>(lroundf(sinf(rad) * radar::kAircraftNoseLenPx));
  *tip_y = cy - static_cast<int>(lroundf(cosf(rad) * radar::kAircraftNoseLenPx));
}

void drawHeadingTriangle(int cx, int cy, float heading_deg, uint16_t color) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  const float sin_h = sinf(rad);
  const float cos_h = cosf(rad);

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y);

  const int base_x =
      cx - static_cast<int>(lroundf(sin_h * static_cast<float>(radar::kAircraftTailLenPx)));
  const int base_y =
      cy + static_cast<int>(lroundf(cos_h * static_cast<float>(radar::kAircraftTailLenPx)));

  const int wing_x = static_cast<int>(lroundf(cos_h * radar::kAircraftTailHalfPx));
  const int wing_y = static_cast<int>(lroundf(sin_h * radar::kAircraftTailHalfPx));

  s_draw->fillTriangle(tip_x, tip_y, base_x + wing_x, base_y + wing_y,
                       base_x - wing_x, base_y - wing_y, color);
}

void drawSpeedVector(int cx, int cy, float heading_deg, float track_deg,
                     float gs_knots, uint16_t color) {
  const int len = speedLineLengthPx(gs_knots);
  if (len <= 0) {
    return;
  }

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y);

  constexpr float kDegToRad = 0.01745329252f;
  const float rad = track_deg * kDegToRad;
  int ex = tip_x + static_cast<int>(lroundf(sinf(rad) * len));
  int ey = tip_y - static_cast<int>(lroundf(cosf(rad) * len));
  clipPointToOuterRing(tip_x, tip_y, &ex, &ey);
  if (ex == tip_x && ey == tip_y) {
    return;
  }
  s_draw->drawWideLine(tip_x, tip_y, ex, ey, radar::kAircraftTrackLineHalfWidth,
                       color);
}

void applyTagStyle() {
  if (s_tag_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_tag_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_tag_gfx);
  }
}

int measureTagBlockWidth(const services::adsb::Aircraft& plane) {
  applyTagStyle();
  int max_w = 0;
  if (plane.callsign[0] != '\0') {
    const int w = s_draw->textWidth(plane.callsign);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (plane.type[0] != '\0') {
    const int w = s_draw->textWidth(plane.type);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (plane.alt[0] != '\0') {
    const int w = s_draw->textWidth(plane.alt);
    if (w > max_w) {
      max_w = w;
    }
  }
  return max_w;
}

void drawAircraftTag(int x, int y, const services::adsb::Aircraft& plane) {
  initTagLabelMetrics();
  applyTagStyle();

  const int line_h = s_draw->fontHeight();
  const int block_w = measureTagBlockWidth(plane);
  const int block_h = line_h * 3;
  int ly = y - block_h / 2;

  const int symbol_half =
      radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;
  // West (left): tag toward center on the right; east (right): tag on the left.
  const bool tag_on_right = x < radar::kCenterX;
  int anchor_x = 0;
  if (tag_on_right) {
    anchor_x = x + symbol_half + radar::kAircraftLabelGapPx;
    anchor_x = std::min(anchor_x, radar::kSize - block_w - 1);
    s_draw->setTextDatum(textdatum_t::top_left);
  } else {
    anchor_x = x - symbol_half - radar::kAircraftLabelGapPx;
    anchor_x = std::max(anchor_x, block_w + 1);
    s_draw->setTextDatum(textdatum_t::top_right);
  }
  ly = std::max(1, std::min(ly, radar::kSize - block_h - 1));

  if (plane.callsign[0] != '\0') {
    s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
    s_draw->drawString(plane.callsign, anchor_x, ly);
  }
  ly += line_h;

  if (plane.type[0] != '\0') {
    s_draw->setTextColor(radar::kColorTagType, radar::kColorBackground);
    s_draw->drawString(plane.type, anchor_x, ly);
  }
  ly += line_h;

  if (plane.alt[0] != '\0') {
    s_draw->setTextColor(radar::kColorTagAltitude, radar::kColorBackground);
    s_draw->drawString(plane.alt, anchor_x, ly);
  }
}

struct AircraftDrawItem {
  size_t index = 0;
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

struct BeyondDotDrawItem {
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

void sortDrawItemsFarFirst(AircraftDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const AircraftDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void sortBeyondDotsFarFirst(BeyondDotDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const BeyondDotDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

constexpr int kSelectionRingRadiusPx =
    radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx + 3;

/** Target-lock ring around the aircraft currently shown in the info panel. */
void drawSelectionRing(int x, int y) {
  s_draw->drawCircle(x, y, kSelectionRingRadiusPx, radar::kColorLabel);
}

bool isSelected(const services::adsb::Aircraft& plane) {
  return s_has_selection && strcmp(plane.callsign, s_selected.callsign) == 0;
}

void drawAircraft() {
  initLabelMetrics();

  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();

  AircraftDrawItem items[services::adsb::kMaxAircraft];
  BeyondDotDrawItem dots[services::adsb::kMaxAircraft];
  size_t draw_count = 0;
  size_t dot_count = 0;

  for (size_t i = 0; i < n; ++i) {
    float dx_km = 0.0f;
    float dy_km = 0.0f;
    float dist_km = 0.0f;
    offsetKmFromCenter(planes[i].lat, planes[i].lon, &dx_km, &dy_km, &dist_km);

    if (isInsideOuterRingKm(dist_km)) {
      int x = 0;
      int y = 0;
      latLonToScreen(planes[i].lat, planes[i].lon, &x, &y);
      items[draw_count].index = i;
      items[draw_count].x = x;
      items[draw_count].y = y;
      items[draw_count].dist_sq = distSqFromCenter(x, y);
      ++draw_count;
      continue;
    }

    int dot_x = 0;
    int dot_y = 0;
    if (!beyondRingEdgeDotFromLatLon(planes[i].lat, planes[i].lon, &dot_x,
                                     &dot_y)) {
      continue;
    }
    dots[dot_count].x = dot_x;
    dots[dot_count].y = dot_y;
    dots[dot_count].dist_sq = distSqFromCenter(dot_x, dot_y);
    ++dot_count;
  }

  sortBeyondDotsFarFirst(dots, dot_count);
  for (size_t d = 0; d < dot_count; ++d) {
    drawBeyondRingDot(dots[d].x, dots[d].y);
  }

  sortDrawItemsFarFirst(items, draw_count);
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    const int x = items[d].x;
    const int y = items[d].y;
    drawSpeedVector(x, y, planes[i].nose_deg, planes[i].track_deg,
                    planes[i].gs_knots, radar::kColorTrackVector);
    drawHeadingTriangle(x, y, planes[i].nose_deg, radar::kColorAircraft);
    if (isSelected(planes[i])) {
      drawSelectionRing(x, y);
    }
  }
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    drawAircraftTag(items[d].x, items[d].y, planes[i]);
  }
}

void applyCardinalStyle() {
  if (s_cardinal_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_cardinal_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_cardinal_gfx);
  }
}

void applyScaleStyle() {
  if (s_scale_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_scale_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_scale_gfx);
  }
}

void applyOuterRangeStyle() {
  if (s_outer_range_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_outer_range_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_outer_range_gfx);
  }
}

void drawCardinalLabel(const char* text, int x, int y, textdatum_t datum) {
  applyCardinalStyle();
  s_draw->setTextDatum(datum);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawScaleLabelWithBackground(const char* text, int x, int y) {
  applyScaleStyle();
  s_draw->setTextDatum(textdatum_t::middle_right);

  const int tw = s_draw->textWidth(text);
  const int th = s_draw->fontHeight();
  constexpr int kPadX = 3;
  constexpr int kPadY = 2;

  const int left = x - tw - kPadX;
  const int top = y - th / 2 - kPadY;

  s_draw->fillRect(left, top, tw + kPadX * 2, th + kPadY * 2,
                   radar::kColorBackground);
  s_draw->setTextColor(radar::kColorGrid, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawGridRing(int cx, int cy, int r, uint16_t color) {
  if (r <= 0) {
    return;
  }
  const int thickness =
      std::max(1, static_cast<int>(radar::kGridStrokeHalfWidth * 2.0f));
  for (int i = 0; i < thickness && r - i > 0; ++i) {
    s_draw->drawCircle(cx, cy, r - i, color);
  }
}

void drawRings(int cx, int cy, int outer_radius) {
  for (int i = 1; i <= radar::kRingCount; ++i) {
    const int r = (outer_radius * i) / radar::kRingCount;
    drawGridRing(cx, cy, r, radar::kColorGrid);
  }
}

void drawCrosshairs(int cx, int cy, int radius, uint16_t color) {
  s_draw->drawWideLine(cx, cy - radius, cx, cy + radius,
                       radar::kGridStrokeHalfWidth, color);
  s_draw->drawWideLine(cx - radius, cy, cx + radius, cy,
                       radar::kGridStrokeHalfWidth, color);
}

void drawCenterDot(int cx, int cy) {
  s_draw->fillSmoothCircle(cx, cy, radar::kCenterDotRadius, radar::kColorCenter);
}

void drawCardinalLabels() {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int edge = radar::kSize - 1;

  drawCardinalLabel("N", cx, radar::kCardinalNorthOffsetY, textdatum_t::top_center);
  drawCardinalLabel("S", cx, edge + radar::kCardinalSouthOffsetY,
                    textdatum_t::bottom_center);
  drawCardinalLabel("W", 0, cy, textdatum_t::middle_left);
  drawCardinalLabel("E", edge, cy, textdatum_t::middle_right);
}

int scaleLabelAnchorX(int cx, int outer_radius) {
  return cx + outer_radius - radar::kScaleGapFromOuterRing;
}

void drawScaleLabel(int cx, int cy, int outer_radius) {
  char scale_label[12];
  radar::formatCurrentRing3Label(scale_label, sizeof(scale_label));
  drawScaleLabelWithBackground(scale_label,
                               scaleLabelAnchorX(cx, outer_radius), cy);
}

/** Outer-ring range (full display radius), top-right corner — outside the circle. */
void drawOuterRangeLabel() {
  char label[12];
  radar::formatRing3Label(label, sizeof(label), radar::rangeCurrent().outer_km,
                          radar::useMiles());
  applyOuterRangeStyle();
  s_draw->setTextDatum(textdatum_t::top_right);
  s_draw->setTextColor(radar::kColorGrid, radar::kColorBackground);
  constexpr int kMarginPx = 4;
  s_draw->drawString(label, radar::kSize - 1 - kMarginPx, kMarginPx);
}

template <typename Gfx>
void drawStaticGrid(Gfx& gfx) {
  initLabelMetrics();
  const DrawScope scope(gfx);
  displayFontEnsureLoaded(gfx);
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int grid_r = radar::kGridOuterRadius;

  gfx.fillScreen(radar::kColorBackground);
  drawRings(cx, cy, grid_r);
  drawCrosshairs(cx, cy, grid_r, radar::kColorGrid);
  initPalette();
  runway::drawLargeAirportRunways(gfx);
  drawCenterDot(cx, cy);
  drawCardinalLabels();
  drawScaleLabel(cx, cy, grid_r);
  drawOuterRangeLabel();
  gfx.setTextDatum(textdatum_t::top_left);
}

bool ensureFrameSprite() {
  if (s_frame_ready) {
    return true;
  }
  s_frame.setColorDepth(16);
  if (!s_frame.createSprite(radar::kSize, radar::kSize)) {
    Serial.println("radar: frame sprite alloc failed");
    return false;
  }
  // Sprite only covers a kSize x kSize square; clear the full panel once so any
  // leftover status-screen background beyond that square (taller panels) goes black.
  tft.fillScreen(config::kColorBlack);
  s_frame_ready = true;
  return true;
}

// --- Aircraft info panel (bottom strip on panels taller than radar::kSize) ---

bool s_lookup_ready = false;
bool s_route_valid = false;
char s_route_origin[8] = "";
char s_route_destination[8] = "";
char s_route_origin_country[4] = "";
char s_route_origin_municipality[24] = "";
char s_route_destination_country[4] = "";
char s_route_destination_municipality[24] = "";
bool s_aircraft_valid = false;
char s_aircraft_type[24] = "";
char s_aircraft_manufacturer[20] = "";

void clearLookupState() {
  s_lookup_ready = false;
  s_route_valid = false;
  s_route_origin[0] = '\0';
  s_route_destination[0] = '\0';
  s_route_origin_country[0] = '\0';
  s_route_origin_municipality[0] = '\0';
  s_route_destination_country[0] = '\0';
  s_route_destination_municipality[0] = '\0';
  s_aircraft_valid = false;
  s_aircraft_type[0] = '\0';
  s_aircraft_manufacturer[0] = '\0';
}

/** Shrink `full` until it fits max_width_px (current font), appending "...". */
void fitLineWithEllipsis(const char* full, char* out, size_t out_len, int max_width_px) {
  strncpy(out, full, out_len - 1);
  out[out_len - 1] = '\0';
  if (tft.textWidth(out) <= max_width_px) {
    return;
  }
  const size_t len = strlen(full);
  for (size_t n = len; n > 0; --n) {
    snprintf(out, out_len, "%.*s...", static_cast<int>(n), full);
    if (tft.textWidth(out) <= max_width_px) {
      return;
    }
  }
  strncpy(out, "...", out_len - 1);
  out[out_len - 1] = '\0';
}

/** Same in-ring filter as drawAircraft(), so cycling matches what's on screen. */
size_t buildVisibleIndices(size_t* out_indices, size_t max_out) {
  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  size_t count = 0;
  for (size_t i = 0; i < n && count < max_out; ++i) {
    float dx_km = 0.0f;
    float dy_km = 0.0f;
    float dist_km = 0.0f;
    offsetKmFromCenter(planes[i].lat, planes[i].lon, &dx_km, &dy_km, &dist_km);
    if (isInsideOuterRingKm(dist_km)) {
      out_indices[count++] = i;
    }
  }
  return count;
}

/** Keep the selected aircraft's telemetry live across periodic refetches. */
void refreshSelectedSnapshot() {
  if (!s_has_selection) {
    return;
  }
  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  for (size_t i = 0; i < n; ++i) {
    if (strcmp(planes[i].callsign, s_selected.callsign) == 0) {
      s_selected = planes[i];
      s_selected_present = true;
      return;
    }
  }
  // Not in the latest fetch (drifted out of range/lost) — keep last-known
  // snapshot rather than yanking the panel out from under the user, but flag
  // it as stale so the panel can grey out and show GONE.
  if (s_selected_present) {
    s_lost_since_ms = millis();
  }
  s_selected_present = false;
}

void applyPanelStyle() {
  initPanelLabelMetrics();
  if (s_panel_use_vlw) {
    displayFontSetSmoothSize(tft, s_panel_vlw_size);
  } else {
    displayFontSetBitmap(tft, s_panel_gfx);
  }
}

void drawSelectionPanel() {
  const int panel_y = radar::kSize;
  const int panel_h = config::kDisplayHeight - radar::kSize;
  if (panel_h <= 0) {
    return;  // square panel (e.g. round GC9A01) — no room for a strip
  }

  tft.fillRect(0, panel_y, config::kDisplayWidth, panel_h, config::kColorBlack);
  if (!s_has_selection) {
    return;
  }

  displayFontEnsureLoaded(tft);
  applyPanelStyle();
  tft.setTextDatum(textdatum_t::top_left);
  const uint16_t text_color =
      s_selected_present ? config::kTextOnBlack : config::kColorGreyText;
  tft.setTextColor(text_color, config::kColorBlack);

  constexpr int kPanelPadX = 6;
  constexpr int kPanelLineGap = 2;
  const int line_h = tft.fontHeight();
  int y = panel_y + kPanelPadX;

  const int y1 = y;
  char line1[24];
  snprintf(line1, sizeof(line1), "%s  %s", s_selected.callsign,
           s_selected.type[0] != '\0' ? s_selected.type : "?");
  tft.drawString(line1, kPanelPadX, y);
  y += line_h + kPanelLineGap;

  const int y2 = y;
  char line2[24];
  snprintf(line2, sizeof(line2), "%s   %d kt", s_selected.alt,
           static_cast<int>(lroundf(s_selected.gs_knots)));
  tft.drawString(line2, kPanelPadX, y);
  y += line_h + kPanelLineGap;

  // "GONE Xs" shares the bottom row with the municipality line; reserve room
  // for it on the right so the truncated municipality text doesn't run
  // under it.
  char gone_text[16] = "";
  int gone_reserved_w = 0;
  if (!s_selected_present) {
    snprintf(gone_text, sizeof(gone_text), "GONE %lus",
             (millis() - s_lost_since_ms) / 1000);
    gone_reserved_w = tft.textWidth(gone_text) + kPanelPadX;
  }

  char line3[24];
  char line4[48];
  line4[0] = '\0';
  if (!s_lookup_ready) {
    strncpy(line3, "Looking up route...", sizeof(line3) - 1);
    line3[sizeof(line3) - 1] = '\0';
  } else if (s_route_valid) {
    snprintf(line3, sizeof(line3), "%s -> %s", s_route_origin, s_route_destination);
    char full[64];
    snprintf(full, sizeof(full), "%s-%s -> %s-%s", s_route_origin_country,
             s_route_origin_municipality, s_route_destination_country,
             s_route_destination_municipality);
    fitLineWithEllipsis(full, line4, sizeof(line4),
                        config::kDisplayWidth - kPanelPadX * 2 - gone_reserved_w);
  } else {
    strncpy(line3, "Route unavailable", sizeof(line3) - 1);
    line3[sizeof(line3) - 1] = '\0';
  }
  tft.drawString(line3, kPanelPadX, y);
  y += line_h + kPanelLineGap;

  if (line4[0] != '\0') {
    tft.drawString(line4, kPanelPadX, y);
  }

  if (!s_selected_present) {
    tft.setTextDatum(textdatum_t::top_right);
    tft.setTextColor(config::kTextOnBlack, config::kColorBlack);
    tft.drawString(gone_text, config::kDisplayWidth - kPanelPadX, y);
    tft.setTextColor(text_color, config::kColorBlack);
    tft.setTextDatum(textdatum_t::top_left);
  }

  if (s_lookup_ready && s_aircraft_valid) {
    tft.setTextDatum(textdatum_t::top_right);
    const int right_x = config::kDisplayWidth - kPanelPadX;
    if (s_aircraft_type[0] != '\0') {
      tft.drawString(s_aircraft_type, right_x, y1);
    }
    if (s_aircraft_manufacturer[0] != '\0') {
      tft.drawString(s_aircraft_manufacturer, right_x, y2);
    }
  }

  tft.setTextDatum(textdatum_t::top_left);
}

// Double-buffered frame: composite the grid AND aircraft into the off-screen
// sprite, then blit it to the panel in a single pushSprite. Because the panel
// is updated in one pass, labels never show an erase/redraw gap — no flicker.
void renderFrame() {
  refreshSelectedSnapshot();
  drawStaticGrid(s_frame);  // opens its own DrawScope(s_frame)
  {
    const DrawScope scope(s_frame);
    drawAircraft();
  }
  s_frame.pushSprite(0, 0);
  tft.setTextDatum(textdatum_t::top_left);
  drawSelectionPanel();
}

}  // namespace

void radarDisplayDraw() {
  initPalette();
  initLabelMetrics();

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  // Fallback when the sprite can't be allocated: draw straight to the panel.
  const DrawScope scope(tft);
  drawStaticGrid(tft);
  drawAircraft();
  tft.setTextDatum(textdatum_t::top_left);
  drawSelectionPanel();
}

void radarDisplayRefreshAircraft() {
  initPalette();

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  radarDisplayDraw();
}

bool radarSelectionCycleNext(char* callsign_out, size_t callsign_out_len,
                             char* hex_out, size_t hex_out_len) {
  size_t visible[services::adsb::kMaxAircraft];
  const size_t visible_count =
      buildVisibleIndices(visible, services::adsb::kMaxAircraft);
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();

  int current_pos = -1;
  if (s_has_selection) {
    for (size_t v = 0; v < visible_count; ++v) {
      if (strcmp(planes[visible[v]].callsign, s_selected.callsign) == 0) {
        current_pos = static_cast<int>(v);
        break;
      }
    }
  }

  const int next_pos = current_pos + 1;
  if (visible_count == 0 || next_pos >= static_cast<int>(visible_count)) {
    s_has_selection = false;
    clearLookupState();
    return false;
  }

  s_selected = planes[visible[next_pos]];
  s_has_selection = true;
  s_selected_present = true;
  clearLookupState();
  if (callsign_out != nullptr && callsign_out_len > 0) {
    strncpy(callsign_out, s_selected.callsign, callsign_out_len - 1);
    callsign_out[callsign_out_len - 1] = '\0';
  }
  if (hex_out != nullptr && hex_out_len > 0) {
    strncpy(hex_out, s_selected.hex, hex_out_len - 1);
    hex_out[hex_out_len - 1] = '\0';
  }
  return true;
}

bool radarSelectionLookupNeedsFetch() { return s_has_selection && !s_lookup_ready; }

void radarSelectionSetRoute(const char* for_callsign, bool found,
                            const services::flight_route::Route& route) {
  if (!s_has_selection || strcmp(s_selected.callsign, for_callsign) != 0) {
    return;  // selection moved on while the lookup was in flight
  }
  s_lookup_ready = true;
  s_route_valid = found;
  if (!found) {
    return;
  }
  strncpy(s_route_origin, route.origin, sizeof(s_route_origin) - 1);
  s_route_origin[sizeof(s_route_origin) - 1] = '\0';
  strncpy(s_route_destination, route.destination, sizeof(s_route_destination) - 1);
  s_route_destination[sizeof(s_route_destination) - 1] = '\0';
  strncpy(s_route_origin_country, route.origin_country, sizeof(s_route_origin_country) - 1);
  s_route_origin_country[sizeof(s_route_origin_country) - 1] = '\0';
  strncpy(s_route_origin_municipality, route.origin_municipality,
          sizeof(s_route_origin_municipality) - 1);
  s_route_origin_municipality[sizeof(s_route_origin_municipality) - 1] = '\0';
  strncpy(s_route_destination_country, route.destination_country,
          sizeof(s_route_destination_country) - 1);
  s_route_destination_country[sizeof(s_route_destination_country) - 1] = '\0';
  strncpy(s_route_destination_municipality, route.destination_municipality,
          sizeof(s_route_destination_municipality) - 1);
  s_route_destination_municipality[sizeof(s_route_destination_municipality) - 1] = '\0';
}

void radarSelectionSetAircraftInfo(const char* for_callsign, bool found,
                                   const services::aircraft_info::AircraftInfo& info) {
  if (!s_has_selection || strcmp(s_selected.callsign, for_callsign) != 0) {
    return;  // selection moved on while the lookup was in flight
  }
  s_lookup_ready = true;
  s_aircraft_valid = found;
  if (!found) {
    return;
  }
  strncpy(s_aircraft_type, info.type, sizeof(s_aircraft_type) - 1);
  s_aircraft_type[sizeof(s_aircraft_type) - 1] = '\0';
  strncpy(s_aircraft_manufacturer, info.manufacturer, sizeof(s_aircraft_manufacturer) - 1);
  s_aircraft_manufacturer[sizeof(s_aircraft_manufacturer) - 1] = '\0';
}

}  // namespace ui
