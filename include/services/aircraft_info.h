#pragma once

namespace services::aircraft_info {

struct AircraftInfo {
  char type[24] = "";
  char manufacturer[20] = "";
};

/**
 * One-shot blocking lookup of the registered aircraft type/manufacturer for
 * a Mode-S hex address (api.adsbdb.com). Returns false (fields left empty)
 * if the hex is unknown to adsbdb (common for GA/private aircraft).
 */
bool fetchAircraftInfo(const char* hex, AircraftInfo* out);

}  // namespace services::aircraft_info
