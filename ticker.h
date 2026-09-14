#ifndef TICKER_H
#define TICKER_H

#include <Arduino.h>
#include <time.h>

// Call once from setup(), after WiFi is up.
void tickerBegin();

// Call every loop(). Non-blocking: fetches at most one symbol per
// TICKER_FETCH_INTERVAL_MS, and only while the US market is open.
void tickerPump();

// True during US market hours: 09:30-16:00 US Eastern, Mon-Fri.
// Takes the device's Dublin local time and converts arithmetically —
// the global timezone belongs to the clock and must not be changed.
bool tickerIsMarketOpen(const struct tm &nowLocalDublin);

// True if an API key was entered on the setup portal.
bool tickerHasApiKey();

// Number of tracked symbols.
int tickerSymbolCount();

// Reads one symbol's latest state. validOut is false until a first
// successful fetch. Returns false if index is out of range.
bool tickerEntry(int index, String &symbolOut, float &priceOut,
                 float &pctOut, bool &validOut);

#endif
