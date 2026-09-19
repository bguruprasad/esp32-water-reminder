#ifndef CHART_H
#define CHART_H

#include <Arduino.h>

// Call once from setup(), after WiFi is up.
void chartBegin();

// Call every loop(). Non-blocking: fetches at most one symbol per
// CHART_FETCH_INTERVAL_MS, rotating through the symbol list.
void chartPump();

// Number of stored points for a symbol. 0 until a first fetch succeeds.
int chartPointCount(int symbolIndex);

// Reads one close price. Returns false if either index is out of range.
bool chartPoint(int symbolIndex, int i, float &closeOut);

// Low, high and previous close for a symbol's stored series, for scaling
// and for the dashed reference line. Returns false with no valid data.
bool chartRange(int symbolIndex, float &loOut, float &hiOut, float &prevCloseOut);

#endif
