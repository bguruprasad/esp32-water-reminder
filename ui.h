#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>

// Paints the whole idle screen from scratch, clearing it first: the
// 12-hour clock with AM/PM, a divider, the weekday, and a status pill.
// Call this once when entering the idle state (e.g. at boot, or when
// returning from an alert) - NOT every tick, since the full clear is
// what causes visible flicker.
void uiDrawIdleScreen(TFT_eSPI &tft, const struct tm &nowLocal, const String &statusLine);

// Updates the idle screen in place, repainting only the parts whose
// content actually changed since the last call. Never clears the
// screen, so it can be called freely without flicker. Returns quickly
// when nothing has changed.
//
// Call uiDrawIdleScreen() first to establish the screen; this function
// tracks what it last drew and diffs against it.
void uiUpdateIdleScreen(TFT_eSPI &tft, const struct tm &nowLocal, const String &statusLine);

// The alert screen cycles through these background colors while flashing.
enum AlertFlashColor { ALERT_RED, ALERT_AMBER, ALERT_GREEN };

// Renders one frame of the full-screen reminder alert in the given flash
// color: a small drawn glass-of-water icon, "DRINK WATER", no subtext.
// Caller drives the color cycling and the dismiss timing/logic (tap or
// timeout) - this just paints one frame per call.
void uiDrawAlertScreen(TFT_eSPI &tft, AlertFlashColor color);

// Renders one symbol panel: the symbol and percent change on one line,
// the price below, then a line chart of the day. slot 0 is the upper
// panel, slot 1 the lower. Repaints only that panel's own strip, so it
// never disturbs the clock above it.
//
// Call this only when the displayed pair actually changes, not every
// tick. Repainting on every frame is what made the old scrolling band
// flicker.
void uiDrawChartPanel(TFT_eSPI &tft, int slot, int symbolIndex);

// Blanks both panel strips and the divider between them.
void uiClearChartPanels(TFT_eSPI &tft);

#endif
