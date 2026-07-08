#pragma once

#include <cstddef>

namespace services::location {

constexpr size_t kMaxLocations = 5;
constexpr size_t kLocationNameMaxLen = 8;

struct LocationSlot {
  char name[kLocationNameMaxLen + 1] = "";
  double lat = 0.0;
  double lon = 0.0;
};

/** Load saved locations + active index from NVS, or use config defaults. Call once before WiFi setup. */
void init();

/** Active location's coordinates (factory defaults if nothing stored). */
double lat();
double lon();
/** Active location's name; "" if unnamed. */
const char* name();

/** Read-only slot access (0..kMaxLocations-1) for portal field prefill. */
const LocationSlot& slot(size_t index);

/**
 * Advance to the next slot with a non-empty name, wrapping around. No-op
 * (returns false) if fewer than 2 slots are named. Persists the new active
 * index on change.
 */
bool cycleNext();

/**
 * Parse portal strings for all kMaxLocations slots, validate, persist to
 * NVS, and update runtime state. An empty name clears that slot regardless
 * of its lat/lon strings.
 */
bool saveLocationsFromStrings(const char* const names[kMaxLocations],
                              const char* const lats[kMaxLocations],
                              const char* const lons[kMaxLocations]);

/** Clear all stored locations and the active index (e.g. with WiFi credential reset). */
void clear();

}  // namespace services::location
