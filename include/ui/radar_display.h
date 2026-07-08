#pragma once

#include <cstddef>

#include "services/aircraft_info.h"
#include "services/flight_route.h"

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/**
 * Cycle the info-panel selection through currently in-ring aircraft, with a
 * "none" stop between passes. Resets any pending/known lookups for the new
 * selection. Returns true and fills callsign_out/hex_out if an aircraft is
 * now selected; returns false (out params untouched) when the selection is
 * "none".
 */
bool radarSelectionCycleNext(char* callsign_out, size_t callsign_out_len,
                             char* hex_out, size_t hex_out_len);

/** Reset the selection to "none" (e.g. when the radar center location changes). */
void radarSelectionClear();

/** True if an aircraft is selected and its route/aircraft info haven't been looked up yet. */
bool radarSelectionLookupNeedsFetch();

/**
 * Attach lookup results to the currently selected aircraft. Both are ignored
 * if the selection has since changed (stale result from an earlier lookup).
 */
void radarSelectionSetRoute(const char* for_callsign, bool found,
                            const services::flight_route::Route& route);
void radarSelectionSetAircraftInfo(const char* for_callsign, bool found,
                                   const services::aircraft_info::AircraftInfo& info);

}  // namespace ui
