#include "services/flight_route.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cstring>

namespace services::flight_route {

namespace {

constexpr char kApiBase[] = "https://api.adsbdb.com/v0/callsign/";
constexpr unsigned long kRequestTimeoutMs = 6000;

void copyJsonField(const JsonObject& obj, const char* key, char* out, size_t out_len) {
  out[0] = '\0';
  if (!obj[key].is<const char*>()) {
    return;
  }
  strncpy(out, obj[key].as<const char*>(), out_len - 1);
  out[out_len - 1] = '\0';
}

void copyAirportCode(const JsonObject& airport, char* out, size_t out_len) {
  out[0] = '\0';
  const char* code = nullptr;
  if (airport["iata_code"].is<const char*>()) {
    code = airport["iata_code"].as<const char*>();
  } else if (airport["icao_code"].is<const char*>()) {
    code = airport["icao_code"].as<const char*>();
  }
  if (code == nullptr) {
    return;
  }
  strncpy(out, code, out_len - 1);
  out[out_len - 1] = '\0';
}

}  // namespace

bool fetchRoute(const char* callsign, Route* out) {
  out->origin[0] = '\0';
  out->destination[0] = '\0';
  out->origin_country[0] = '\0';
  out->origin_municipality[0] = '\0';
  out->destination_country[0] = '\0';
  out->destination_municipality[0] = '\0';

  if (callsign == nullptr || callsign[0] == '\0') {
    return false;
  }

  String url = kApiBase;
  url += callsign;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("route: http.begin failed");
    return false;
  }
  http.setConnectTimeout(kRequestTimeoutMs);
  http.setTimeout(kRequestTimeoutMs);

  Serial.printf("route: GET %s (heap free=%u max_alloc=%u)\n", url.c_str(),
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  const unsigned long t_start = millis();
  const int code = http.GET();
  const unsigned long t_headers = millis();
  if (code != HTTP_CODE_OK) {
    Serial.printf("route: HTTP %d after %lu ms\n", code, t_headers - t_start);
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  const unsigned long t_done = millis();
  http.end();
  Serial.printf("route: HTTP %d — headers %lu ms, body %lu ms, total %lu ms (heap "
                "free=%u max_alloc=%u)\n",
                code, t_headers - t_start, t_done - t_headers, t_done - t_start,
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (err) {
    Serial.printf("route: JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonObject flightroute = doc["response"]["flightroute"];
  JsonObject origin = flightroute["origin"];
  JsonObject destination = flightroute["destination"];
  if (origin.isNull() || destination.isNull()) {
    Serial.println("route: no route on file for callsign");
    return false;
  }

  copyAirportCode(origin, out->origin, sizeof(out->origin));
  copyAirportCode(destination, out->destination, sizeof(out->destination));
  copyJsonField(origin, "country_iso_name", out->origin_country, sizeof(out->origin_country));
  copyJsonField(origin, "municipality", out->origin_municipality,
                sizeof(out->origin_municipality));
  copyJsonField(destination, "country_iso_name", out->destination_country,
                sizeof(out->destination_country));
  copyJsonField(destination, "municipality", out->destination_municipality,
                sizeof(out->destination_municipality));
  return out->origin[0] != '\0' && out->destination[0] != '\0';
}

}  // namespace services::flight_route
