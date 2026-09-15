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

// Renders one page of the share-price band: two symbols side by side, in
// bold, held static. Pages are instant-swapped by the caller rather than
// scrolled — a scrolling marquee had to clear and repaint the whole strip
// ~25 times a second, which visibly flickered.
//
// Call this only when the page actually changes (or its data does), not
// every tick. pageIndex is taken modulo the available page count.
void uiDrawTickerPage(TFT_eSPI &tft, int pageIndex);

// Number of pages needed to show every symbol two at a time. Returns 0
// when there is nothing to show (e.g. no API key stored).
int uiTickerPageCount();

// Blanks the band's strip — used when the market is shut.
void uiClearTickerBand(TFT_eSPI &tft);

#endif
