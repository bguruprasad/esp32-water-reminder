#include "ui.h"
#include "config.h"
#include "chart.h"
#include "ticker.h"
#include <string.h> // strcmp, for diffing what's already on screen
// FreeSansBold24pt7b is already pulled in transitively via TFT_eSPI.h ->
// gfxfont.h (which includes all 48 GFXFF fonts when LOAD_GFXFF is
// enabled) - including it again here would redefine its symbols, since
// the font header has no include guard of its own.

// Converts 24-hour tm_hour to a 12-hour display hour (1-12) and returns
// whether it's AM. Noon and midnight both map to 12, per convention.
static int to12Hour(int hour24, bool *isAm) {
  *isAm = hour24 < 12;
  int hour12 = hour24 % 12;
  return hour12 == 0 ? 12 : hour12;
}

// Portrait layout. The clock sits near the top; the lower two thirds
// belong to the two chart panels. Everything below the clock derives
// from IDLE_CLOCK_Y, so nudging that one constant moves the block.
//
// These are arithmetic against a 320px-tall screen, not observed values.
// The landscape constants they replace took several rounds on hardware
// to settle, so expect these to need the same.
static const int IDLE_CLOCK_Y   = 34;   // vertical middle of the big digits
static const int IDLE_AMPM_GAP  = 6;
// FreeSansBold9pt7b yAdvance.
static const int IDLE_SMALL_FONT_H = 22;

// What was last painted, so uiUpdateIdleScreen() can repaint only what
// changed instead of clearing the screen every tick (a full clear once
// per second is what caused visible flicker).
static bool idleScreenPainted = false;
static char lastDigits[6] = "";
static char lastAmpm[3] = "";

static void formatClock(const struct tm &nowLocal, char *digitsOut, size_t digitsLen,
                        const char **ampmOut) {
  bool isAm;
  int hour12 = to12Hour(nowLocal.tm_hour, &isAm);
  snprintf(digitsOut, digitsLen, "%d:%02d", hour12, nowLocal.tm_min);
  *ampmOut = isAm ? "AM" : "PM";
}

// Paints the clock row (big digits + small AM/PM beside them), centred
// as one composite block. Erases the row's full-width band first so a
// shorter string (e.g. "12:59" -> "1:00") leaves no stale pixels.
static void paintClockRow(TFT_eSPI &tft, const char *digitsBuf, const char *ampmStr) {
  // Two-tier clock, both runs in the bold FreeSans family (the built-in
  // numeric Font 6 is thin-stroked, not bold): large digits at 24pt, a
  // smaller AM/PM label at 12pt beside them, like a real clock face.
  //
  // Both runs are drawn with L_BASELINE on ONE shared baseline, so the
  // AM/PM sits flush with the bottom of the digits.
  //
  // L_BASELINE, not BL_DATUM: for a free font drawString() first does
  // poY += glyph_ab, then BL_DATUM subtracts (glyph_ab + glyph_bb)  -
  // netting poY - glyph_bb, i.e. it anchors the DESCENDER bottom, not
  // the baseline. The 24pt digits have a deeper descender than the 12pt
  // label, so sharing a BL_DATUM line pushed the label's baseline lower
  // than the digits'. L_BASELINE subtracts exactly glyph_ab, cancelling
  // the adjustment and making poY the true baseline for both sizes.
  const int digitsHeight = 56; // FreeSansBold24pt7b yAdvance
  int bandTop = IDLE_CLOCK_Y - digitsHeight / 2 - 2;
  tft.fillRect(0, bandTop, TFT_HRES, digitsHeight + 4, TFT_BLACK);

  // textWidth() measures the currently-set free font, so each run must
  // be measured under its own font or the composite centring is wrong.
  tft.setFreeFont(&FreeSansBold24pt7b);
  int digitsWidth = tft.textWidth(digitsBuf);
  tft.setFreeFont(&FreeSansBold12pt7b);
  int ampmWidth = tft.textWidth(ampmStr);

  int totalWidth = digitsWidth + IDLE_AMPM_GAP + ampmWidth;
  int digitsLeftX = TFT_HRES / 2 - totalWidth / 2;
  // Baseline sits an ascent below the band top (~3/4 of yAdvance for this
  // face), leaving the descender space below it inside the band.
  int baselineY = IDLE_CLOCK_Y - digitsHeight / 2 + (digitsHeight * 3) / 4;

  tft.setTextDatum(L_BASELINE);

  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(digitsBuf, digitsLeftX, baselineY);

  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(ampmStr, digitsLeftX + digitsWidth + IDLE_AMPM_GAP, baselineY);

  tft.setFreeFont(NULL); // back to the numbered bitmap fonts
  tft.setTextDatum(MC_DATUM);
}

void uiDrawIdleScreen(TFT_eSPI &tft, const struct tm &nowLocal, const String &statusLine) {
  (void)statusLine; // no status pill in portrait; kept for call-site compatibility
  tft.fillScreen(TFT_BLACK);

  char digitsBuf[6]; // "12:59" + nul
  const char *ampmStr;
  formatClock(nowLocal, digitsBuf, sizeof(digitsBuf), &ampmStr);

  paintClockRow(tft, digitsBuf, ampmStr);

  snprintf(lastDigits, sizeof(lastDigits), "%s", digitsBuf);
  snprintf(lastAmpm, sizeof(lastAmpm), "%s", ampmStr);
  idleScreenPainted = true;
}

void uiUpdateIdleScreen(TFT_eSPI &tft, const struct tm &nowLocal, const String &statusLine) {
  if (!idleScreenPainted) { // nothing on screen to diff against yet
    uiDrawIdleScreen(tft, nowLocal, statusLine);
    return;
  }

  char digitsBuf[6];
  const char *ampmStr;
  formatClock(nowLocal, digitsBuf, sizeof(digitsBuf), &ampmStr);

  if (strcmp(digitsBuf, lastDigits) != 0 || strcmp(ampmStr, lastAmpm) != 0) {
    paintClockRow(tft, digitsBuf, ampmStr);
    snprintf(lastDigits, sizeof(lastDigits), "%s", digitsBuf);
    snprintf(lastAmpm, sizeof(lastAmpm), "%s", ampmStr);
  }
}

// Draws a small glass-of-water icon (outline + water fill) centered at
// (cx, cy), sized to fit within a roughly iconWidth x (iconWidth*1.17)
// box - matches the tapered-glass shape used in the design mockup.
// TFT_eSPI has no emoji font, so this is a native shape-drawn icon.
static void drawGlassIcon(TFT_eSPI &tft, int cx, int cy, int iconWidth) {
  int halfTop = iconWidth / 2;
  int halfBottom = (iconWidth * 13) / 32; // glass tapers slightly inward
  int height = (iconWidth * 7) / 6;

  int topY = cy - height / 2;
  int bottomY = cy + height / 2;
  int topLeftX = cx - halfTop;
  int topRightX = cx + halfTop;
  int bottomLeftX = cx - halfBottom;
  int bottomRightX = cx + halfBottom;

  // Water fill: from ~55% down to the bottom, tapered to match the sides.
  int waterTopY = topY + (height * 55) / 100;
  int waterTopLeftX = topLeftX + ((bottomLeftX - topLeftX) * 55) / 100;
  int waterTopRightX = topRightX + ((bottomRightX - topRightX) * 55) / 100;
  tft.fillTriangle(waterTopLeftX, waterTopY, waterTopRightX, waterTopY, bottomLeftX, bottomY, TFT_CYAN);
  tft.fillTriangle(waterTopRightX, waterTopY, bottomRightX, bottomY, bottomLeftX, bottomY, TFT_CYAN);

  // Glass outline: two side lines + bottom line (top is implicitly open).
  tft.drawLine(topLeftX, topY, bottomLeftX, bottomY, TFT_WHITE);
  tft.drawLine(topRightX, topY, bottomRightX, bottomY, TFT_WHITE);
  tft.drawLine(bottomLeftX, bottomY, bottomRightX, bottomY, TFT_WHITE);
}

void uiDrawAlertScreen(TFT_eSPI &tft, AlertFlashColor color) {
  uint32_t bg;
  switch (color) {
    case ALERT_RED:   bg = TFT_RED;    break;
    case ALERT_AMBER: bg = TFT_ORANGE; break;
    case ALERT_GREEN: bg = TFT_GREEN;  break;
    default:          bg = TFT_RED;    break;
  }

  tft.fillScreen(bg);

  int iconCx = TFT_HRES / 2;
  int iconCy = TFT_VRES / 2 - 60;
  drawGlassIcon(tft, iconCx, iconCy, 34);

  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextColor(TFT_WHITE, bg);
  // Pulled up 18px to sit closer under the glass icon (which stays at
  // its current height); line spacing between the two words is unchanged.
  tft.drawString("DRINK", TFT_HRES / 2, TFT_VRES / 2 + 2);
  tft.drawString("WATER", TFT_HRES / 2, TFT_VRES / 2 + 52);
  tft.setFreeFont(NULL); // restore default GLCD/bitmap font for other screens
}

// Two-line band. The idle stack above now ends at y~171 (pill bottom),
// so the band starts at 178 and runs to the bottom edge: 62px, enough
// for two 22px lines of FreeSansBold9pt7b plus padding. A single 35px
// line could not fit "GOOGL $333.08 ^0.24%" across a half-width column
// at bold weight, which is what caused the columns to overlap.
// Two stacked panels fill the lower two thirds of the portrait screen.
// Each is 118px tall: a header line, a price line, then the chart.
//
// Arithmetic against a 320px-tall screen, not observed. Expect to nudge
// these once the panel is connected.
static const int PANEL_TOP[2]   = { 84, 202 };
static const int PANEL_H        = 118;
static const int PANEL_HDR_DY   = 16;   // header baseline within the panel
static const int PANEL_PRICE_DY = 44;   // price baseline within the panel
static const int PANEL_CHART_DY = 56;   // chart top within the panel
static const int PANEL_CHART_H  = 58;

void uiClearChartPanels(TFT_eSPI &tft) {
  tft.fillRect(0, PANEL_TOP[0], TFT_HRES, PANEL_H * 2, TFT_BLACK);
}

// Draws a small up/down triangle - shape-drawn, since there is no emoji
// font (the same reason the alert screen's glass is drawn by hand).
static void drawTrendArrow(TFT_eSPI &tft, int cx, int cy, bool up, uint16_t color) {
  const int halfW = 4, halfH = 4;
  if (up) {
    tft.fillTriangle(cx, cy - halfH, cx - halfW, cy + halfH, cx + halfW, cy + halfH, color);
  } else {
    tft.fillTriangle(cx, cy + halfH, cx - halfW, cy - halfH, cx + halfW, cy - halfH, color);
  }
}

// Maps a price to a y coordinate inside a panel's chart area.
static int chartY(float v, float lo, float hi, int top, int h) {
  if (hi <= lo) return top + h / 2;
  const int pad = 3;
  float t = (hi - v) / (hi - lo);
  return top + pad + (int)(t * (h - pad * 2));
}

void uiDrawChartPanel(TFT_eSPI &tft, int slot, int symbolIndex) {
  if (slot < 0 || slot > 1) return;
  const int top = PANEL_TOP[slot];
  tft.fillRect(0, top, TFT_HRES, PANEL_H, TFT_BLACK);

  // Edge-to-edge divider above the lower panel.
  if (slot == 1) tft.drawFastHLine(0, top - 2, TFT_HRES, TFT_DARKGREY);

  String sym;
  float price, pct;
  bool valid;
  if (!tickerEntry(symbolIndex, sym, price, pct, valid)) return;

  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(sym, 6, top + PANEL_HDR_DY);

  if (valid) {
    bool up = pct >= 0.0f;
    uint16_t c = up ? TFT_GREEN : TFT_RED;
    char pctBuf[16];
    snprintf(pctBuf, sizeof(pctBuf), "%.2f%%", pct < 0 ? -pct : pct);
    int pctW = tft.textWidth(pctBuf);
    tft.setTextColor(c, TFT_BLACK);
    tft.drawString(pctBuf, TFT_HRES - 6 - pctW, top + PANEL_HDR_DY);
    drawTrendArrow(tft, TFT_HRES - 12 - pctW, top + PANEL_HDR_DY + 8, up, c);

    char priceBuf[16];
    snprintf(priceBuf, sizeof(priceBuf), "%.2f", price);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString("$", 6, top + PANEL_PRICE_DY);
    int dollarW = tft.textWidth("$");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(priceBuf, 6 + dollarW, top + PANEL_PRICE_DY);
  } else {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("--", 6, top + PANEL_PRICE_DY);
  }
  tft.setFreeFont(NULL);
  tft.setTextDatum(MC_DATUM);

  // Chart.
  float lo, hi, prevClose;
  int n = chartPointCount(symbolIndex);
  if (n < 2 || !chartRange(symbolIndex, lo, hi, prevClose)) return;

  const int cTop = top + PANEL_CHART_DY;

  // Dashed reference line at the previous close, so points above it are
  // up on the day. Drawn first so the series sits over it.
  if (prevClose > 0) {
    int ry = chartY(prevClose, lo, hi, cTop, PANEL_CHART_H);
    for (int x = 0; x < TFT_HRES; x += 7) {
      tft.drawFastHLine(x, ry, 3, TFT_DARKGREY);
    }
  }

  uint16_t lineCol = TFT_GREEN;
  float lastClose;
  if (chartPoint(symbolIndex, n - 1, lastClose) && prevClose > 0 && lastClose < prevClose) {
    lineCol = TFT_RED;
  }

  float a, b;
  for (int i = 1; i < n; i++) {
    if (!chartPoint(symbolIndex, i - 1, a) || !chartPoint(symbolIndex, i, b)) continue;
    int x0 = ((i - 1) * (TFT_HRES - 1)) / (n - 1);
    int x1 = (i * (TFT_HRES - 1)) / (n - 1);
    tft.drawLine(x0, chartY(a, lo, hi, cTop, PANEL_CHART_H),
                 x1, chartY(b, lo, hi, cTop, PANEL_CHART_H), lineCol);
  }
}
