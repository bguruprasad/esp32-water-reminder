#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>

// Paints the whole idle screen from scratch, clearing it first: the
// 12-hour clock with AM/PM, a divider, the weekday, and a status pill.
// Call this once when entering the idle state (e.g. at boot, or when
// returning from an alert) — NOT every tick, since the full clear is
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
// timeout) — this just paints one frame per call.
void uiDrawAlertScreen(TFT_eSPI &tft, AlertFlashColor color);

// Renders the share-price band along the bottom strip (y=200-235),
// scrolled left by scrollOffsetPx. Repaints only its own strip, never
// the whole screen, so it does not reintroduce flicker.
void uiDrawTickerBand(TFT_eSPI &tft, int scrollOffsetPx);

// Blanks the band's strip — used when the market is shut.
void uiClearTickerBand(TFT_eSPI &tft);

#endif
