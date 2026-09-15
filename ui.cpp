#include "ui.h"
#include "config.h"
#include "ticker.h"
#include <string.h> // strcmp, for diffing what's already on screen
// FreeSansBold24pt7b is already pulled in transitively via TFT_eSPI.h ->
// gfxfont.h (which includes all 48 GFXFF fonts when LOAD_GFXFF is
// enabled) — including it again here would redefine its symbols, since
// the font header has no include guard of its own.

static const char *WEEKDAY_NAMES[7] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

// Converts 24-hour tm_hour to a 12-hour display hour (1-12) and returns
// whether it's AM. Noon and midnight both map to 12, per convention.
static int to12Hour(int hour24, bool *isAm) {
  *isAm = hour24 < 12;
  int hour12 = hour24 % 12;
  return hour12 == 0 ? 12 : hour12;
}

// Fixed vertical layout of the idle screen. Kept as named constants so
// the full paint and the partial update agree on where things live.
static const int IDLE_CLOCK_Y   = TFT_VRES / 2 - 34;          // vertical middle of the big digits
static const int IDLE_DIVIDER_Y = IDLE_CLOCK_Y + 34;
// Weekday sits 2px higher than it used to, which opens up both the gap
// above it (divider -> weekday) and the one below (weekday -> pill).
static const int IDLE_DAY_Y     = IDLE_DIVIDER_Y + 18;
static const int IDLE_PILL_Y    = IDLE_DAY_Y + 32;
static const int IDLE_PILL_H    = 26;
static const int IDLE_AMPM_GAP  = 6;
// FreeSansBold9pt7b yAdvance — used for the weekday and the pill label.
static const int IDLE_SMALL_FONT_H = 22;

// What was last painted, so uiUpdateIdleScreen() can repaint only what
// changed instead of clearing the screen every tick (a full clear once
// per second is what caused visible flicker).
static bool idleScreenPainted = false;
static char lastDigits[6] = "";
static char lastAmpm[3] = "";
static char lastWeekday[10] = "";
static char lastStatus[32] = "";

static const char *weekdayName(const struct tm &nowLocal) {
  return (nowLocal.tm_wday >= 0 && nowLocal.tm_wday < 7)
           ? WEEKDAY_NAMES[nowLocal.tm_wday] : "?";
}

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
  // poY += glyph_ab, then BL_DATUM subtracts (glyph_ab + glyph_bb) —
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

static void paintWeekday(TFT_eSPI &tft, const char *weekday) {
  const int h = IDLE_SMALL_FONT_H;
  tft.fillRect(0, IDLE_DAY_Y - h / 2 - 2, TFT_HRES, h + 4, TFT_BLACK);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
  tft.drawString(weekday, TFT_HRES / 2, IDLE_DAY_Y);
  tft.setFreeFont(NULL);
}

// Status pill: a rounded rect sized to the text, with the status string
// centred inside it. The pill's width tracks the text, so the whole band
// is cleared first rather than just the pill's own footprint.
static void paintStatusPill(TFT_eSPI &tft, const char *statusLine) {
  tft.fillRect(0, IDLE_PILL_Y - IDLE_PILL_H / 2 - 2, TFT_HRES, IDLE_PILL_H + 4, TFT_BLACK);

  tft.setFreeFont(&FreeSansBold9pt7b);
  int pillWidth = tft.textWidth(statusLine) + 24;
  int pillX = TFT_HRES / 2 - pillWidth / 2;
  tft.fillRoundRect(pillX, IDLE_PILL_Y - IDLE_PILL_H / 2, pillWidth, IDLE_PILL_H,
                    IDLE_PILL_H / 2, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_NAVY);
  // Nudged up 2px: MC_DATUM centres on the font's full cell (ascent plus
  // descender), but this label has no descenders, so centring on the cell
  // leaves its visual mass sitting low in the pill.
  tft.drawString(statusLine, TFT_HRES / 2, IDLE_PILL_Y - 2);
  tft.setFreeFont(NULL);
}

void uiDrawIdleScreen(TFT_eSPI &tft, const struct tm &nowLocal, const String &statusLine) {
  tft.fillScreen(TFT_BLACK);

  char digitsBuf[6]; // "12:59" + nul
  const char *ampmStr;
  formatClock(nowLocal, digitsBuf, sizeof(digitsBuf), &ampmStr);
  const char *weekday = weekdayName(nowLocal);

  paintClockRow(tft, digitsBuf, ampmStr);

  // Divider line beneath the clock. Static, so only the full paint draws it.
  // 7/25 of the width per side (~179px total), 40% longer than the 1/5 it was.
  int dividerHalfWidth = (TFT_HRES * 7) / 25;
  tft.drawFastHLine(TFT_HRES / 2 - dividerHalfWidth, IDLE_DIVIDER_Y,
                    dividerHalfWidth * 2, TFT_DARKGREY);

  paintWeekday(tft, weekday);
  paintStatusPill(tft, statusLine.c_str());

  snprintf(lastDigits, sizeof(lastDigits), "%s", digitsBuf);
  snprintf(lastAmpm, sizeof(lastAmpm), "%s", ampmStr);
  snprintf(lastWeekday, sizeof(lastWeekday), "%s", weekday);
  snprintf(lastStatus, sizeof(lastStatus), "%s", statusLine.c_str());
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
  const char *weekday = weekdayName(nowLocal);
  const char *status = statusLine.c_str();

  if (strcmp(digitsBuf, lastDigits) != 0 || strcmp(ampmStr, lastAmpm) != 0) {
    paintClockRow(tft, digitsBuf, ampmStr);
    snprintf(lastDigits, sizeof(lastDigits), "%s", digitsBuf);
    snprintf(lastAmpm, sizeof(lastAmpm), "%s", ampmStr);
  }

  if (strcmp(weekday, lastWeekday) != 0) {
    paintWeekday(tft, weekday);
    snprintf(lastWeekday, sizeof(lastWeekday), "%s", weekday);
  }

  if (strcmp(status, lastStatus) != 0) {
    paintStatusPill(tft, status);
    snprintf(lastStatus, sizeof(lastStatus), "%s", status);
  }
}

// Draws a small glass-of-water icon (outline + water fill) centered at
// (cx, cy), sized to fit within a roughly iconWidth x (iconWidth*1.17)
// box — matches the tapered-glass shape used in the design mockup.
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

static const int TICKER_BAND_TOP = 200;
static const int TICKER_BAND_H   = 35;

void uiClearTickerBand(TFT_eSPI &tft) {
  tft.fillRect(0, TICKER_BAND_TOP, TFT_HRES, TICKER_BAND_H, TFT_BLACK);
}

// Draws a small up/down triangle — shape-drawn, since there is no emoji
// font (the same reason the alert screen's glass is drawn by hand).
static void drawTrendArrow(TFT_eSPI &tft, int cx, int cy, bool up, uint16_t color) {
  const int halfW = 4, halfH = 4;
  if (up) {
    tft.fillTriangle(cx, cy - halfH, cx - halfW, cy + halfH, cx + halfW, cy + halfH, color);
  } else {
    tft.fillTriangle(cx, cy + halfH, cx - halfW, cy - halfH, cx + halfW, cy - halfH, color);
  }
}

int uiDrawTickerBand(TFT_eSPI &tft, int scrollOffsetPx) {
  uiClearTickerBand(tft);

  int textY = TICKER_BAND_TOP + TICKER_BAND_H / 2;
  tft.setTextDatum(ML_DATUM);
  tft.setFreeFont(NULL);

  if (!tickerHasApiKey()) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Ticker: no API key", 8, textY, 2);
    tft.setTextDatum(MC_DATUM);
    return 0; // nothing laid out, so nothing for the caller to wrap against
  }

  const int entryGap = 28;
  int x = -scrollOffsetPx;
  int totalWidth = 0;

  for (int i = 0; i < tickerSymbolCount(); i++) {
    String sym;
    float price, pct;
    bool valid;
    if (!tickerEntry(i, sym, price, pct, valid)) continue;

    char buf[40];
    if (valid) {
      snprintf(buf, sizeof(buf), "%s %.2f", sym.c_str(), price);
    } else {
      snprintf(buf, sizeof(buf), "%s --", sym.c_str());
    }

    // Measure the entry's FULL width up front, independently of whether
    // it is visible. Accumulating the arrow/percent width only inside the
    // visibility branch would give an entry a different stride depending
    // on where it happened to be on screen, so the layout — and the total
    // width the caller wraps against — would drift as it scrolled.
    int textW = tft.textWidth(buf, 2);
    char pctBuf[16];
    int entryW = textW;
    if (valid) {
      snprintf(pctBuf, sizeof(pctBuf), "%.2f%%", pct < 0 ? -pct : pct);
      entryW += 16 + tft.textWidth(pctBuf, 2);
    }

    // Draw only what is on screen; keeps the loop cheap.
    if (x + entryW > 0 && x < TFT_HRES) {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(buf, x, textY, 2);

      if (valid) {
        bool up = pct >= 0.0f;
        uint16_t c = up ? TFT_GREEN : TFT_RED;
        drawTrendArrow(tft, x + textW + 8, textY, up, c);
        tft.setTextColor(c, TFT_BLACK);
        tft.drawString(pctBuf, x + textW + 16, textY, 2);
      }
    }

    x += entryW + entryGap;
    totalWidth += entryW + entryGap;
  }

  tft.setTextDatum(MC_DATUM); // restore the datum the other screens expect
  return totalWidth;
}
