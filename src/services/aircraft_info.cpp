#include "services/aircraft_info.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cstring>

namespace services::aircraft_info {

namespace {

constexpr char kApiBase[] = "https://api.adsbdb.com/v0/aircraft/";
constexpr unsigned long kRequestTimeoutMs = 6000;

void copyJsonField(const JsonObject& obj, const char* key, char* out, size_t out_len) {
  out[0] = '\0';
  if (!obj[key].is<const char*>()) {
    return;
  }
  strncpy(out, obj[key].as<const char*>(), out_len - 1);
  out[out_len - 1] = '\0';
}

}  // namespace

bool fetchAircraftInfo(const char* hex, AircraftInfo* out) {
  out->type[0] = '\0';
  out->manufacturer[0] = '\0';

  if (hex == nullptr || hex[0] == '\0') {
    return false;
  }

  String url = kApiBase;
  url += hex;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("aircraft_info: http.begin failed");
    return false;
  }
  http.setConnectTimeout(kRequestTimeoutMs);
  http.setTimeout(kRequestTimeoutMs);

  Serial.printf("aircraft_info: GET %s (heap free=%u max_alloc=%u)\n", url.c_str(),
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  const unsigned long t_start = millis();
  const int code = http.GET();
  const unsigned long t_headers = millis();
  if (code != HTTP_CODE_OK) {
    Serial.printf("aircraft_info: HTTP %d after %lu ms\n", code, t_headers - t_start);
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  const unsigned long t_done = millis();
  http.end();
  Serial.printf("aircraft_info: HTTP %d — headers %lu ms, body %lu ms, total %lu ms "
                "(heap free=%u max_alloc=%u)\n",
                code, t_headers - t_start, t_done - t_headers, t_done - t_start,
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (err) {
    Serial.printf("aircraft_info: JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonObject aircraft = doc["response"]["aircraft"];
  if (aircraft.isNull()) {
    Serial.println("aircraft_info: no data on file for hex");
    return false;
  }

  copyJsonField(aircraft, "type", out->type, sizeof(out->type));
  copyJsonField(aircraft, "manufacturer", out->manufacturer, sizeof(out->manufacturer));
  return out->type[0] != '\0' || out->manufacturer[0] != '\0';
}

}  // namespace services::aircraft_info
