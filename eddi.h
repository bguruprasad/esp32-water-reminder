#ifndef EDDI_H
#define EDDI_H

#include <Arduino.h>

// Sentinel for "never fetched" or "currently unreachable". Deliberately
// distinct from every real sta code, so the UI can show an honest
// unknown rather than a stale or invented state.
static const int EDDI_UNKNOWN = -1;

// Call once from setup(), after WiFi is up.
void eddiBegin();

// Call every loop(). Fetches at most once per 30 seconds, which sits
// well inside the community guidance of one request per 10 seconds for
// the myenergi API (exceeding it risks a temporary IP ban). The fetch
// itself is synchronous and capped by a 5 second timeout, so a loop tick
// can stall by at most that much, at most once per interval.
void eddiPump();

// Last observed sta code, or EDDI_UNKNOWN. The codes come from the
// myenergi API and match the owner's Python controller:
//   1 Paused (on, waiting for surplus)
//   3 Diverting (actively heating)
//   4 Boosting
//   5 Max Temp Reached
//   6 Stopped
int eddiStatus();

// Short display word for a sta code. Deliberately short: the full
// "Max Temp Reached" beside a weekday overflows the 240px row.
const char *eddiStatusText(int sta);

// Display colour for a sta code: green when heating, amber when on but
// idle, grey when stopped, dim grey when unknown.
uint16_t eddiStatusColor(int sta);

// True once after the status changes, cleared on read, so the UI
// repaints only when there is something new to show.
bool eddiChanged();

#endif
