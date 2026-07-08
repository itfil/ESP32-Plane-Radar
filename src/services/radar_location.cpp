#include "services/radar_location.h"

#include <Preferences.h>
#include <cstdlib>
#include <cstring>

#include "config.h"

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyActive[] = "active";
// Legacy single-location keys, read once for migration into slot 0.
constexpr char kLegacyKeyLat[] = "lat";
constexpr char kLegacyKeyLon[] = "lon";

LocationSlot s_slots[kMaxLocations];
size_t s_active = 0;

void keyFor(size_t index, const char* suffix, char* out, size_t out_len) {
  snprintf(out, out_len, "loc%u%s", static_cast<unsigned>(index), suffix);
}

bool parseCoord(const char* text, double* out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const double v = strtod(text, &end);
  if (end == text || (end != nullptr && *end != '\0')) {
    return false;
  }
  *out = v;
  return true;
}

bool validLatLon(double lat, double lon) {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

void copyName(const char* src, char* out) {
  out[0] = '\0';
  if (src == nullptr) {
    return;
  }
  strncpy(out, src, kLocationNameMaxLen);
  out[kLocationNameMaxLen] = '\0';
}

void persistSlot(size_t index) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  char key[16];
  keyFor(index, "n", key, sizeof(key));
  prefs.putString(key, s_slots[index].name);
  keyFor(index, "lat", key, sizeof(key));
  prefs.putDouble(key, s_slots[index].lat);
  keyFor(index, "lon", key, sizeof(key));
  prefs.putDouble(key, s_slots[index].lon);
  prefs.end();
}

void persistActive() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  prefs.putUChar(kKeyActive, static_cast<uint8_t>(s_active));
  prefs.end();
}

bool isNamed(size_t index) { return s_slots[index].name[0] != '\0'; }

/** First named slot, or 0 if none are named. */
size_t firstNamedOrZero() {
  for (size_t i = 0; i < kMaxLocations; ++i) {
    if (isNamed(i)) {
      return i;
    }
  }
  return 0;
}

}  // namespace

void init() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    s_slots[0].lat = config::kDefaultRadarLat;
    s_slots[0].lon = config::kDefaultRadarLon;
    return;
  }

  char key[16];
  bool any_new_key = false;
  for (size_t i = 0; i < kMaxLocations; ++i) {
    keyFor(i, "n", key, sizeof(key));
    if (prefs.isKey(key)) {
      any_new_key = true;
      const String name = prefs.getString(key, "");
      copyName(name.c_str(), s_slots[i].name);
    }
    keyFor(i, "lat", key, sizeof(key));
    const double lat = prefs.getDouble(key, s_slots[i].lat);
    keyFor(i, "lon", key, sizeof(key));
    const double lon = prefs.getDouble(key, s_slots[i].lon);
    if (validLatLon(lat, lon)) {
      s_slots[i].lat = lat;
      s_slots[i].lon = lon;
    }
  }

  if (!any_new_key) {
    // Fresh NVS under the new schema — migrate the legacy single location
    // into slot 0 (unnamed, matching prior single-location behavior).
    if (prefs.isKey(kLegacyKeyLat) && prefs.isKey(kLegacyKeyLon)) {
      const double lat = prefs.getDouble(kLegacyKeyLat, config::kDefaultRadarLat);
      const double lon = prefs.getDouble(kLegacyKeyLon, config::kDefaultRadarLon);
      if (validLatLon(lat, lon)) {
        s_slots[0].lat = lat;
        s_slots[0].lon = lon;
      } else {
        s_slots[0].lat = config::kDefaultRadarLat;
        s_slots[0].lon = config::kDefaultRadarLon;
      }
    } else {
      s_slots[0].lat = config::kDefaultRadarLat;
      s_slots[0].lon = config::kDefaultRadarLon;
    }
  }

  s_active = prefs.getUChar(kKeyActive, 0);
  if (s_active >= kMaxLocations || !isNamed(s_active)) {
    s_active = firstNamedOrZero();
  }

  prefs.end();
}

double lat() { return s_slots[s_active].lat; }

double lon() { return s_slots[s_active].lon; }

const char* name() { return s_slots[s_active].name; }

const LocationSlot& slot(size_t index) { return s_slots[index]; }

bool cycleNext() {
  size_t named_count = 0;
  for (size_t i = 0; i < kMaxLocations; ++i) {
    if (isNamed(i)) {
      ++named_count;
    }
  }
  if (named_count < 2) {
    return false;
  }

  size_t next = s_active;
  do {
    next = (next + 1) % kMaxLocations;
  } while (!isNamed(next));

  s_active = next;
  persistActive();
  return true;
}

bool saveLocationsFromStrings(const char* const names[kMaxLocations],
                              const char* const lats[kMaxLocations],
                              const char* const lons[kMaxLocations]) {
  bool any_valid = false;
  for (size_t i = 0; i < kMaxLocations; ++i) {
    char name[kLocationNameMaxLen + 1];
    copyName(names[i], name);

    if (name[0] == '\0') {
      s_slots[i].name[0] = '\0';
      s_slots[i].lat = 0.0;
      s_slots[i].lon = 0.0;
      persistSlot(i);
      continue;
    }

    double lat = 0.0;
    double lon = 0.0;
    if (!parseCoord(lats[i], &lat) || !parseCoord(lons[i], &lon) ||
        !validLatLon(lat, lon)) {
      Serial.printf("Location %u: invalid lat/lon — slot not saved\n",
                    static_cast<unsigned>(i));
      continue;
    }

    strncpy(s_slots[i].name, name, kLocationNameMaxLen);
    s_slots[i].name[kLocationNameMaxLen] = '\0';
    s_slots[i].lat = lat;
    s_slots[i].lon = lon;
    persistSlot(i);
    any_valid = true;
    Serial.printf("Location %u saved: %s (%.6f, %.6f)\n", static_cast<unsigned>(i),
                  s_slots[i].name, lat, lon);
  }

  if (!isNamed(s_active)) {
    s_active = firstNamedOrZero();
    persistActive();
  }

  return any_valid;
}

void clear() {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    char key[16];
    for (size_t i = 0; i < kMaxLocations; ++i) {
      keyFor(i, "n", key, sizeof(key));
      prefs.remove(key);
      keyFor(i, "lat", key, sizeof(key));
      prefs.remove(key);
      keyFor(i, "lon", key, sizeof(key));
      prefs.remove(key);
    }
    prefs.remove(kKeyActive);
    prefs.remove(kLegacyKeyLat);
    prefs.remove(kLegacyKeyLon);
    prefs.end();
  }

  for (size_t i = 0; i < kMaxLocations; ++i) {
    s_slots[i] = LocationSlot{};
  }
  s_slots[0].lat = config::kDefaultRadarLat;
  s_slots[0].lon = config::kDefaultRadarLon;
  s_active = 0;
}

}  // namespace services::location
