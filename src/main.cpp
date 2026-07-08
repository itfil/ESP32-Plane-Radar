/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "hardware/touch_input.h"
#include "services/adsb_client.h"
#include "services/aircraft_info.h"
#include "services/flight_route.h"
#include "services/radar_location.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void fetchAndDrawAircraft();

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}

void onSelectTap() {
  char callsign[9] = "";
  char hex[8] = "";
  const bool selected =
      ui::radarSelectionCycleNext(callsign, sizeof(callsign), hex, sizeof(hex));
  if (g_radar_visible) {
    ui::radarDisplayDraw();
  }
  if (!selected || !ui::radarSelectionLookupNeedsFetch()) {
    return;
  }

  // Free the ADS-B keep-alive connection's TLS memory first — otherwise these
  // two handshakes to api.adsbdb.com can fail under heap pressure while it's
  // held open. The next scheduled ADS-B poll just reconnects.
  services::adsb::releaseConnection();

  const unsigned long lookup_start = millis();

  services::flight_route::Route route;
  const bool route_found = services::flight_route::fetchRoute(callsign, &route);
  ui::radarSelectionSetRoute(callsign, route_found, route);

  services::aircraft_info::AircraftInfo info;
  const bool info_found = services::aircraft_info::fetchAircraftInfo(hex, &info);
  ui::radarSelectionSetAircraftInfo(callsign, info_found, info);

  Serial.printf("select: lookups for %s (%s) blocked loop() for %lu ms\n", callsign,
                hex, millis() - lookup_start);

  if (g_radar_visible) {
    ui::radarDisplayDraw();
  }
}

void onLocationTap() {
  if (!services::location::cycleNext()) {
    return;  // fewer than 2 named locations — nothing to cycle to
  }
  Serial.printf("Location: %s (%.6f, %.6f)\n", services::location::name(),
                services::location::lat(), services::location::lon());
  ui::radarSelectionClear();
  if (g_radar_visible) {
    g_last_adsb_fetch_ms = millis();
    fetchAndDrawAircraft();
  }
}

void handleTouchInput() {
  touchPoll();
  if (touchConsumeRangeTap()) {
    onRangeTap();
  }
  if (touchConsumeSelectTap()) {
    onSelectTap();
  }
  if (touchConsumeLocationTap()) {
    onLocationTap();
  }
}

/**
 * Passed to adsb_client as its poll callback so a tap that lands entirely
 * within the ADS-B fetch's blocking window (connect + body read) still gets
 * sampled, instead of only being checked before/after the whole fetch.
 */
void pollDuringFetch() {
  wifiLoop();
  touchPoll();
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    handleTouchInput();
    return;
  }
  ui::radarDisplayRefreshAircraft();
  handleBootButton();
  handleTouchInput();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  displayInit();
  touchInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();
  services::adsb::setPollFn(pollDuringFetch);

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
}

void loop() {
  handleBootButton();
  handleTouchInput();
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = millis();
      fetchAndDrawAircraft();
    }
  }

  delay(10);
}
