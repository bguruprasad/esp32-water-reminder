#include "ui.h"
#include "config.h"
#include "chart.h"
#include "ticker.h"
#include "eddi.h"
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
// Raised from 34 to 28 to open the status row below. The gap between the
// clock block and the first chart panel was y=64 to y=84, only 20px, and
// the row needs 26. The panels cannot move down instead: PANEL_TOP[1]
// plus PANEL_H already lands exactly on 320, the bottom of the screen.
static const int IDLE_CLOCK_Y   = 28;   // vertical middle of the big digits
static const int IDLE_AMPM_GAP  = 6;
// FreeSansBold9pt7b yAdvance.
static const int IDLE_SMALL_FONT_H = 22;

// Status row: Eddi pill on the left, weekday on the right, both in the
// Eddi's status colour so the whole row reads as one indicator.
//
// Full weekday names fit because the pill shows a drawn symbol rather
// than a status word. "Max Temp Reached" is about 160px of text before
// pill padding, which left no room beside it; the symbol is about 12px,
// so the longest pairing is now roughly 70 + 90 = 160px of the 240
// available.
// The clock band ends at y=58 and PANEL_TOP[0] is 84, so the row has
// 26px to live in.
//
// Two passes to get here. At 24px centred on 72 the row spanned 60..84,
// and 84 is the exact row the first panel starts painting, so the panel
// covered the pill's bottom edge. At 22px centred on 70 it spanned
// 59..81 - arithmetically clear, but still read as cut off, because the
// corner radius was IDLE_ROW_H/2. On a 22px box that is a fully
// semicircular cap whose bottom arc is the thinnest part of the stroke,
// sitting 1px below the clock band.
//
// 20px centred on 68 spans 58..78: flush under the clock, 6px clear of
// the panel. The radius is now fixed and small, so the outline reads as
// a rounded rectangle rather than a lozenge with vanishing arcs.
static const int IDLE_ROW_Y = 70;   // vertical centre of the row
static const int IDLE_ROW_H = 24;
static const int IDLE_PILL_R = 6;   // corner radius, not half the height

// The label is drawn this many pixels ABOVE the row centre.
//
// Repositioning the pill four times never fixed its missing bottom line,
// because the pill was never the problem: the label was sitting on top
// of that stroke and erasing it. ML_DATUM on a GFX free font centres the
// text using the font's full height, descender space included, so the
// visible glyphs land low in the box - the same datum quirk that pushed
// the AM/PM label below the clock's baseline earlier in this project.
//
// Lifting the text clears the stroke, and the extra row height gives it
// somewhere to go.
static const int IDLE_ROW_TEXT_DY = 2;
static const char *WEEKDAY_NAMES[7] = {"Sunday", "Monday", "Tuesday",
                                       "Wednesday", "Thursday", "Friday",
                                       "Saturday"};

// What was last painted, so uiUpdateIdleScreen() can repaint only what
// changed instead of clearing the screen every tick (a full clear once
// per second is what caused visible flicker).
static bool idleScreenPainted = false;
static char lastDigits[6] = "";
static char lastAmpm[3] = "";
// Seeded to values no real status or weekday can take, so the first
// update always paints the row rather than diffing against a lucky zero.
static int lastRowSta = EDDI_UNKNOWN - 1;
static int lastRowWday = -1;

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

// Transport-control glyphs for the Eddi state, drawn as shapes rather
// than glyphs: TFT_eSPI has no symbol font, which is the same reason
// the alert's glass icon is drawn by hand.
//
// A symbol is about 12px wide against roughly 90px for the longest
// status word, which is what removes the width pressure that previously
// forced both the status and the weekday to be abbreviated.
static void drawEddiSymbol(TFT_eSPI &tft, int sta, int cx, int cy,
                           uint16_t col) {
  const int r = 5; // half-size of the symbol box
  switch (sta) {
    case 3: // Diverting: play
    case 4: // Boosting: play (same shape, colour carries the difference)
      tft.fillTriangle(cx - r, cy - r, cx - r, cy + r, cx + r, cy, col);
      break;
    case 1: // Paused: two bars
    case 5: // Max Temp Reached: also idle, so also bars
      tft.fillRect(cx - r, cy - r, 3, 2 * r, col);
      tft.fillRect(cx + r - 2, cy - r, 3, 2 * r, col);
      break;
    case 6: // Stopped: square
      tft.fillRect(cx - r, cy - r, 2 * r, 2 * r, col);
      break;
    default: // Unknown: hollow square, so it reads as "no reading yet"
      tft.drawRect(cx - r, cy - r, 2 * r, 2 * r, col);
      break;
  }
}

// Paints the status row: the Eddi pill on the left, the weekday on the
// right, both in the status colour. Clears the row's full width first,
// since the pill width tracks its label and a shorter status would
// otherwise leave the tail of a longer one behind.
static void paintStatusRow(TFT_eSPI &tft, const struct tm &nowLocal) {
  tft.fillRect(0, IDLE_ROW_Y - IDLE_ROW_H / 2, TFT_HRES, IDLE_ROW_H, TFT_BLACK);

  int sta = eddiStatus();
  uint16_t col = eddiStatusColor(sta);
  const char *label = eddiStatusText(sta);

  (void)label; // the state now reads as a symbol, not a word

  tft.setFreeFont(&FreeSansBold9pt7b);

  // The pill fills the cleared band; the label is what moves. See
  // IDLE_ROW_TEXT_DY for why the bottom stroke kept disappearing.
  const int rowTop = IDLE_ROW_Y - IDLE_ROW_H / 2;
  const int pillH = IDLE_ROW_H;
  const int textY = IDLE_ROW_Y - IDLE_ROW_TEXT_DY;

  // Left pill: "Eddi" then the state symbol. Outline rather than fill,
  // since a filled pill at this size leaves too little contrast inside.
  tft.setTextDatum(ML_DATUM);
  const int textX = 14;
  int symCx = textX + tft.textWidth("Eddi") + 10; // symbol centre
  int leftPillW = (symCx + 5) - 4 + 10;           // through symbol + padding

  tft.drawRoundRect(4, rowTop, leftPillW, pillH, IDLE_PILL_R, col);
  tft.setTextColor(col, TFT_BLACK);
  tft.drawString("Eddi", textX, textY);
  drawEddiSymbol(tft, sta, symCx, IDLE_ROW_Y, col);

  // Right pill: the weekday, in a matching outline so the row reads as
  // one designed pair rather than a pill beside loose text. Sized to its
  // own text, since "Wednesday" and "Friday" differ by a good 30px.
  const char *day = WEEKDAY_NAMES[nowLocal.tm_wday % 7];
  int dayW = tft.textWidth(day);
  int rightPillW = dayW + 20;
  int rightPillX = TFT_HRES - 4 - rightPillW;

  tft.drawRoundRect(rightPillX, rowTop, rightPillW, pillH,
                    IDLE_PILL_R, col);
  tft.drawString(day, rightPillX + 10, textY);

  tft.setFreeFont(NULL);
  tft.setTextDatum(MC_DATUM);
}

void uiDrawIdleScreen(TFT_eSPI &tft, const struct tm &nowLocal, const String &statusLine) {
  (void)statusLine; // no status pill in portrait; kept for call-site compatibility
  tft.fillScreen(TFT_BLACK);

  char digitsBuf[6]; // "12:59" + nul
  const char *ampmStr;
  formatClock(nowLocal, digitsBuf, sizeof(digitsBuf), &ampmStr);

  paintClockRow(tft, digitsBuf, ampmStr);
  // Unconditional: this function just cleared the whole screen, so the
  // row has to be redrawn regardless of whether its content changed.
  paintStatusRow(tft, nowLocal);

  snprintf(lastDigits, sizeof(lastDigits), "%s", digitsBuf);
  snprintf(lastAmpm, sizeof(lastAmpm), "%s", ampmStr);
  lastRowSta = eddiStatus();
  lastRowWday = nowLocal.tm_wday;
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

  // The row changes far less often than the clock: the Eddi status holds
  // for hours at a time and the weekday for a day, so this repaints
  // almost never. Diffing eddiStatus() rather than consuming
  // eddiChanged() keeps this idempotent - that flag clears on read, and
  // a UI running every tick would swallow it before anything else saw it.
  int sta = eddiStatus();
  if (sta != lastRowSta || nowLocal.tm_wday != lastRowWday) {
    paintStatusRow(tft, nowLocal);
    lastRowSta = sta;
    lastRowWday = nowLocal.tm_wday;
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
