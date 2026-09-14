#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>

// Renders the idle screen: current local time, weekday, and a status
// line (e.g. "Next reminder: 10:30", "Outside work hours",
// "Time not synced"). Clears and redraws the whole screen each call —
// callers should only call this when something has actually changed
// (e.g. once per second), not every loop() tick.
void uiDrawIdleScreen(TFT_eSPI &tft, const struct tm &nowLocal, const String &statusLine);

#endif
