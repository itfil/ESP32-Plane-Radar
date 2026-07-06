#pragma once

#include <cstddef>

namespace services::flight_route {

struct Route {
  char origin[8] = "";
  char destination[8] = "";
  char origin_country[4] = "";
  char origin_municipality[24] = "";
  char destination_country[4] = "";
  char destination_municipality[24] = "";
};

/**
 * One-shot blocking lookup of the scheduled route for a callsign (api.adsbdb.com).
 * Returns false (Route left empty) if the callsign is unknown or has no route on file.
 */
bool fetchRoute(const char* callsign, Route* out);

}  // namespace services::flight_route
