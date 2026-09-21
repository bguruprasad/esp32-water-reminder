#ifndef CHART_H
#define CHART_H

#include <Arduino.h>

// Call once from setup(), after WiFi is up.
void chartBegin();

// Call every loop(). Non-blocking: fetches at most one series per
// CHART_FETCH_INTERVAL_MS, alternating between the day line and the
// 30-minute candles and rotating through the symbol list.
void chartPump();

// --- Day line: the whole session at 15 minute resolution -------------

// Number of stored points for a symbol. 0 until a first fetch succeeds.
int chartPointCount(int symbolIndex);

// Reads one close price. Returns false if either index is out of range.
bool chartPoint(int symbolIndex, int i, float &closeOut);

// Low, high and previous close for a symbol's stored series, for scaling
// and for the dashed reference line. Returns false with no valid data.
bool chartRange(int symbolIndex, float &loOut, float &hiOut, float &prevCloseOut);

// --- Candles: the last 30 minutes at 1 minute resolution -------------
//
// A separate series, deliberately. Drawing candles over a whole day does
// not work on this panel: the day's range across 58px leaves a typical
// 15-minute body under 1.5px, so the body vanishes and only the wick
// reads. Over 30 minutes the range collapses by an order of magnitude
// (measured at 13.9x on a normal day), which puts the median body at
// about 6.5px and makes the candle legible.

// Number of stored candles, 0 until a first candle fetch succeeds.
int chartCandleCount(int symbolIndex);

// Reads one candle. Returns false if either index is out of range.
bool chartCandle(int symbolIndex, int i, float &openOut, float &highOut,
                 float &lowOut, float &closeOut);

// Low and high across the stored candles, taken from the low and high
// columns rather than the closes, so wicks cannot fall outside the
// plotted band. Returns false with no valid data.
bool chartCandleRange(int symbolIndex, float &loOut, float &hiOut);

// The first candle's open, which is the only reference line that means
// anything at this zoom: the previous close sits far off-panel over a
// 30 minute window. Returns false with no valid data.
bool chartCandleOpen(int symbolIndex, float &openOut);

#endif
