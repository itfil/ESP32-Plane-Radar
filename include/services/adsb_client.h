#pragma once

#include <cstddef>

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char callsign[9];
  char type[5];
  char alt[12];
  char hex[8];
};

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

/**
 * Force-close the persistent keep-alive connection to adsb.fi, freeing its
 * TLS session memory. Call before other HTTPS requests (e.g. the SELECT
 * lookups) that need headroom for their own handshake; fetchUpdate()
 * transparently reconnects on its next call.
 */
void releaseConnection();

}  // namespace services::adsb
