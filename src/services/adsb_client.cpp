#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cstring>

#include "config.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 10000;

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
PollFn s_poll_fn = nullptr;

// Persistent (not stack-local): with setReuse(true), keeping these alive
// across calls lets HTTPClient skip the ~1s TLS handshake on most polls by
// reusing the existing keep-alive connection to adsb.fi.
WiFiClientSecure s_client;
HTTPClient s_http;
bool s_http_ready = false;

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

int performGetWithPoll(HTTPClient& http) {
  http.setConnectTimeout(kConnectAttemptMs);
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int code = http.GET();
    if (code > 0) {
      return code;
    }
    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }
    delay(5);
  }
  return HTTPC_ERROR_READ_TIMEOUT;
}

/**
 * Feeds ArduinoJson directly from the response stream (no intermediate
 * String buffer — that was the memory hog that made large responses fail
 * under heap pressure) while still calling pollNetwork() during waits, so
 * WiFiManager's LAN portal stays responsive across a slow multi-second read.
 */
class PollingStreamReader {
 public:
  PollingStreamReader(WiFiClient* stream, unsigned long deadline)
      : stream_(stream), deadline_(deadline) {}

  int read() {
    while (true) {
      if (stream_->available() > 0) {
        const int c = stream_->read();
        if (c >= 0) {
          ++bytes_read_;
          return c;
        }
      }
      pollNetwork();
      if (millis() >= deadline_) {
        timed_out_ = true;
        return -1;
      }
      if (!stream_->connected() && stream_->available() <= 0) {
        disconnected_ = true;
        return -1;
      }
      delay(1);
    }
  }

  size_t bytesRead() const { return bytes_read_; }
  bool timedOut() const { return timed_out_; }
  bool disconnectedEarly() const { return disconnected_; }

 private:
  WiFiClient* stream_;
  unsigned long deadline_;
  size_t bytes_read_ = 0;
  bool timed_out_ = false;
  bool disconnected_ = false;
};

/**
 * adsb.fi returns ~20-30 raw fields per aircraft; we only ever read the ones
 * listed below. Without this filter, ArduinoJson allocates storage for every
 * field of every aircraft, and at larger ranges (more aircraft in view) that
 * routinely exceeded available heap — causing slow reads, NoMemory parse
 * failures, and even a TLS connection failure under pressure. Built once and
 * reused; filters are read-only after construction.
 */
JsonDocument& aircraftFilter() {
  static JsonDocument filter;
  static bool built = false;
  if (!built) {
    JsonObject ac_filter = filter["ac"][0].to<JsonObject>();
    ac_filter["lat"] = true;
    ac_filter["lon"] = true;
    ac_filter["true_heading"] = true;
    ac_filter["mag_heading"] = true;
    ac_filter["track"] = true;
    ac_filter["dir"] = true;
    ac_filter["gs"] = true;
    ac_filter["tas"] = true;
    ac_filter["ias"] = true;
    ac_filter["alt_baro"] = true;
    ac_filter["alt_geom"] = true;
    ac_filter["flight"] = true;
    ac_filter["hex"] = true;
    ac_filter["t"] = true;
    built = true;
  }
  return filter;
}

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "tas", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "ias", &v)) {
    return v;
  }
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out,
                           size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || !obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }

  if (plane["alt_baro"].is<const char*>()) {
    const char* s = plane["alt_baro"].as<const char*>();
    if (strcmp(s, "ground") == 0) {
      strncpy(out, "GND", out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }
  }

  float alt = 0.0f;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    snprintf(out, out_len, "%d ft", static_cast<int>(lroundf(alt)));
  }
}

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(plane, "hex", ac->hex, sizeof(ac->hex));
  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

size_t aircraftCount() { return s_aircraft_count; }

const Aircraft* aircraftList() { return s_aircraft; }

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  const float dist_nm = kmToNauticalMiles(fetch_radius_km);

  String url = kApiBase;
  url += String(center_lat, 6);
  url += "/lon/";
  url += String(center_lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  if (!s_http_ready) {
    s_client.setInsecure();
    s_http.setReuse(true);
    s_http_ready = true;
  }

  if (!s_http.begin(s_client, url)) {
    Serial.println("adsb: http.begin failed");
    return false;
  }

  s_http.setTimeout(kRequestTimeoutMs);
  Serial.printf("adsb: GET %s (heap free=%u max_alloc=%u)\n", url.c_str(),
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  const unsigned long t_start = millis();
  const int code = performGetWithPoll(s_http);
  Serial.printf("adsb: HTTP %d after %lu ms\n", code, millis() - t_start);
  if (code != HTTP_CODE_OK) {
    s_http.end();
    return false;
  }

  WiFiClient* stream = s_http.getStreamPtr();
  if (stream == nullptr) {
    Serial.println("adsb: no response stream");
    s_http.end();
    return false;
  }

  const unsigned long t_body_start = millis();
  PollingStreamReader reader(stream, t_body_start + kRequestTimeoutMs);
  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, reader, DeserializationOption::Filter(aircraftFilter()));
  s_http.end();

  Serial.printf(
      "adsb: body %u bytes in %lu ms (timed_out=%d disconnected=%d) (heap free=%u "
      "max_alloc=%u)\n",
      reader.bytesRead(), millis() - t_body_start, reader.timedOut(),
      reader.disconnectedEarly(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  if (err) {
    Serial.printf("adsb: JSON parse error: %s (body %u bytes)\n", err.c_str(),
                  reader.bytesRead());
    return false;
  }

  JsonArray ac = doc["ac"].as<JsonArray>();
  if (ac.isNull()) {
    s_aircraft_count = 0;
    return true;
  }

  size_t n = 0;
  for (JsonObject plane : ac) {
    if (n >= kMaxAircraft) {
      break;
    }
    if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
      continue;
    }
    if (isOnGround(plane) && !config::kAdsbShowGroundAircraft) {
      continue;
    }

    s_aircraft[n].lat = plane["lat"].as<float>();
    s_aircraft[n].lon = plane["lon"].as<float>();
    s_aircraft[n].nose_deg = pickNoseHeading(plane);
    s_aircraft[n].track_deg = pickTrackHeading(plane);
    s_aircraft[n].gs_knots = pickGroundSpeed(plane);
    fillTagFields(&s_aircraft[n], plane);
    ++n;
  }

  s_aircraft_count = n;
  Serial.printf("adsb: %u aircraft\n", static_cast<unsigned>(n));
  return true;
}

}  // namespace services::adsb
