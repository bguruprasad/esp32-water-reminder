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

// Renders the full-screen reminder alert. Caller is responsible for
// polling touch.Pressed() afterward and returning to the idle screen
// (via uiDrawIdleScreen) once the user taps.
void uiDrawAlertScreen(TFT_eSPI &tft);

#endif
