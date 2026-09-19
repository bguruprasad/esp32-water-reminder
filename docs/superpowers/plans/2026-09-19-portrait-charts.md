# Portrait Orientation and Price Charts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rotate the device to portrait and replace the price band with two stacked symbol panels, each showing a line chart of the day's trading.

**Architecture:** Rotation is two `setRotation` calls plus a constant swap; the 21 existing call sites read `TFT_HRES`/`TFT_VRES` so they follow automatically. The idle layout constants must then be re-derived on hardware. A new `chart.*` module owns candle history from Yahoo and knows nothing about prices, so a Yahoo outage costs charts only. `ui.*` gains a panel renderer that replaces the band; `water-reminder.ino` swaps page state for panel state.

**Tech Stack:** Arduino on ESP32 (core 3.3.11, FQBN `esp32:esp32:esp32`), `TFT_eSPI` (FNK0114B_2P8_240x320_ST7789), `TFT_Touch`, `HTTPClient` + `NetworkClientSecure`, ArduinoJson 7.4.3. No new library installs.

**Spec:** [docs/superpowers/specs/2026-09-14-water-reminder-design.md](../specs/2026-09-14-water-reminder-design.md) - see "Addendum: portrait orientation and price charts".

## Execution order: build now, verify later

The device is not connected while this plan is being implemented. Work is
therefore split:

- **Buildable offline (Tasks 1-6):** every code change, each verified by
  `arduino-cli compile`. Commit as normal.
- **Deferred to one connected session (Task 7):** the partition switch,
  flashing, and every judgement that needs eyes on the panel.

Tasks 1, 2 and 5 each contain a hardware verification step. While the
device is absent, do NOT treat those steps as blocking: mark them
deferred, note what has to be checked, and carry on to the next task.
Task 7 collects them.

Two consequences to be honest about:

- Task 1 picks `setRotation(0)` on the reasonable assumption it puts the
  USB connector at the top. It may be 180 degrees wrong. That is a
  one-constant fix in Task 7, not a rebuild.
- Task 2's layout constants are arithmetic against a 320px-tall screen,
  not observed values. Expect to nudge them in Task 7, the way the
  landscape layout needed several rounds.

## Global Constraints

- Portrait: `TFT_HRES 240`, `TFT_VRES 320`. Display and touch rotation must be the SAME value.
- Rotation is 0 or 2 (both portrait, 180 apart). Which one puts the USB connector at the top is determined on hardware in Task 1, never assumed.
- Chart source: `https://query1.finance.yahoo.com/v8/finance/chart/<SYM>?range=1d&interval=15m`, keyless, plain non-browser user-agent. UNDOCUMENTED - must degrade to prices-only, never break the device.
- Chart fetch: one symbol per 30s, rotating. TLS via `setInsecure()`, both `setConnectTimeout(5000)` and `setTimeout(5000)`.
- Line charts, not candlesticks. A dashed reference line marks the previous close.
- Two panels, one per symbol, separated by an edge-to-edge divider. Weekday and next-reminder rows are removed.
- The reminder is the device's job; charts are decorative and must never block the alert path.
- Charts repaint only their own panel strips. Never `fillScreen` outside `uiDrawIdleScreen`/`uiDrawAlertScreen`.
- Board: FQBN `esp32:esp32:esp32`, port `/dev/cu.usbserial-120`, upload speed `115200`.
- No `Co-Authored-By` trailer in commit messages; the repo history is deliberately without it.
- Pure ASCII in all files and commit messages. No em dashes, en dashes, arrows or symbols.

## File Structure

| File | Responsibility |
|---|---|
| `config.h` (modify) | Swap `TFT_HRES`/`TFT_VRES` to 240/320. |
| `water-reminder.ino` (modify) | `setRotation` value; swap ticker page state for chart panel state. |
| `ui.cpp` / `ui.h` (modify) | Re-derive idle layout constants for a 320px-tall screen; replace `uiDrawTickerPage` with a two-panel renderer. |
| `chart.h` / `chart.cpp` (new) | Candle history: symbol rotation, Yahoo fetch, JSON parse, per-symbol point store. No display code. |
| `ticker.*` (unchanged) | Still owns prices. Charts sit beside it, not inside it. |

**Integration hazards carried from the landscape build:**

1. `uiDrawIdleScreen()` begins with `fillScreen(TFT_BLACK)` and runs on every alert dismissal, erasing the panels. They must be re-shown afterwards, exactly as the band was.
2. The `tickerBandVisible` flag gates only the CLEAR, never the DRAW. Preserve that asymmetry in the panel equivalent or the panels can get stuck blank.

---

## Task 1: Rotate to portrait and find the right rotation value

This task determines a fact that cannot be read from source: whether rotation 0 or 2 puts the USB connector at the top. Everything downstream depends on it.

**Files:**
- Modify: `config.h`
- Modify: `water-reminder.ino`

**Interfaces:**
- Consumes: nothing.
- Produces: the confirmed rotation constant, recorded here for Tasks 3 and 5.

- [ ] **Step 1: Swap the resolution constants in `config.h`**

```c
// Display (FNK0114B 2.8" ST7789, portrait - USB connector at the top)
#define TFT_HRES 240
#define TFT_VRES 320
```

- [ ] **Step 2: Set both rotations in `water-reminder.ino`**

Both calls must use the same value. Start with 0:

```cpp
  tft.setRotation(0);
  touch.setRotation(0);
```

- [ ] **Step 3: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles clean. The layout will look wrong - that is Task 2's job.

- [ ] **Step 4: DEFERRED to Task 7 - which way up is it?**

Requires the device. Do not block on this; `setRotation(0)` is the working assumption until the panel says otherwise.

To check in Task 7: with the board upright and the USB connector at the top, is the text the right way up? If upside down, change BOTH calls to `setRotation(2)` and reflash. Nothing else depends on which value is correct.

- [ ] **Step 5: Commit**

```bash
git add config.h water-reminder.ino
git commit -m "Rotate the display to portrait

240x320 with the USB connector at the top. Display and touch use the
same rotation value, confirmed on hardware rather than assumed, since 0
and 2 are both portrait and differ by 180 degrees.

The idle layout is wrong after this change: every vertical position was
tuned against a 240px-tall screen. Task 2 re-derives them."
```

---

## Task 2: Re-derive the idle layout for a 320px-tall screen

Every constant in `ui.cpp` was tuned by eye against landscape. None of them are valid now. This task is mostly arithmetic followed by hardware confirmation.

**Files:**
- Modify: `ui.cpp`

**Interfaces:**
- Consumes: portrait constants from Task 1.
- Produces: an idle screen that looks correct in portrait, with room below for two chart panels.

- [ ] **Step 1: Replace the layout constants**

The clock moves to the top rather than the middle, since two panels need the lower two thirds. Replace the block starting `static const int IDLE_CLOCK_Y`:

```cpp
// Portrait layout. The clock sits near the top; the lower two thirds
// belong to the two chart panels. Everything below the clock derives
// from IDLE_CLOCK_Y, so nudging that one constant moves the block.
static const int IDLE_CLOCK_Y   = 34;   // vertical middle of the big digits
static const int IDLE_AMPM_GAP  = 6;
// FreeSansBold9pt7b yAdvance.
static const int IDLE_SMALL_FONT_H = 22;
```

Delete `IDLE_DIVIDER_Y`, `IDLE_DAY_Y`, `IDLE_PILL_Y` and `IDLE_PILL_H`: the weekday and status pill are removed in portrait.

- [ ] **Step 2: Strip the weekday and pill from the renderer**

In `uiDrawIdleScreen`, delete the divider `drawFastHLine`, the `paintWeekday` call and the `paintStatusPill` call. Delete the `paintWeekday` and `paintStatusPill` functions and the `lastWeekday` / `lastStatus` tracking variables.

In `uiUpdateIdleScreen`, delete the weekday and status comparison blocks. The clock row comparison stays.

`uiDrawIdleScreen` and `uiUpdateIdleScreen` keep their `statusLine` parameter so the call sites do not change; it is simply unused. Mark it:

```cpp
void uiDrawIdleScreen(TFT_eSPI &tft, const struct tm &nowLocal, const String &statusLine) {
  (void)statusLine; // no status pill in portrait; kept for call-site compatibility
```

- [ ] **Step 3: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles clean, no unused-variable warnings.

- [ ] **Step 4: DEFERRED to Task 7 - confirm the layout**

Requires the device. The constants are arithmetic, not observed.

To check in Task 7: the clock sits near the top, horizontally centred, not clipped at the top edge, with the AM/PM on the digits' baseline and the lower two thirds empty. Expect to nudge `IDLE_CLOCK_Y`; everything below derives from it.

- [ ] **Step 5: Commit**

```bash
git add ui.cpp
git commit -m "Re-derive the idle layout for portrait

The clock moves to the top so the lower two thirds can carry two chart
panels. The weekday and status pill are removed rather than relocated:
portrait has less width and the panels need the room.

Every previous vertical constant was tuned against a 240px-tall screen
and none survived the rotation."
```

---

## Task 3: Chart module - fetch, parse, store

**Files:**
- Create: `chart.h`
- Create: `chart.cpp`

**Interfaces:**
- Consumes: `tickerSymbolCount()`, `tickerEntry()` for the symbol list; `tickerIsMarketOpen()` is NOT used - charts fetch regardless, since Yahoo returns the last session outside hours.
- Produces:
  - `void chartBegin();`
  - `void chartPump();` - non-blocking, at most one fetch per 30s.
  - `int chartPointCount(int symbolIndex);`
  - `bool chartPoint(int symbolIndex, int i, float &closeOut);`
  - `bool chartRange(int symbolIndex, float &loOut, float &hiOut, float &prevCloseOut);`

- [ ] **Step 1: Write `chart.h`**

```c
#ifndef CHART_H
#define CHART_H

#include <Arduino.h>

// Call once from setup(), after WiFi is up.
void chartBegin();

// Call every loop(). Non-blocking: fetches at most one symbol per
// CHART_FETCH_INTERVAL_MS, rotating through the symbol list.
void chartPump();

// Number of stored points for a symbol. 0 until a first fetch succeeds.
int chartPointCount(int symbolIndex);

// Reads one close price. Returns false if either index is out of range.
bool chartPoint(int symbolIndex, int i, float &closeOut);

// Low, high and previous close for a symbol's stored series, for scaling
// and for the dashed reference line. Returns false with no valid data.
bool chartRange(int symbolIndex, float &loOut, float &hiOut, float &prevCloseOut);

#endif
```

- [ ] **Step 2: Write `chart.cpp`**

```cpp
#include "chart.h"
#include "ticker.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>

static const int CHART_MAX_POINTS = 32;   // Yahoo returns 27 at 1d/15m
static const int CHART_MAX_SYMBOLS = 8;
static const unsigned long CHART_FETCH_INTERVAL_MS = 30000;

struct Series {
  float closes[CHART_MAX_POINTS];
  int count = 0;
  float lo = 0, hi = 0, prevClose = 0;
};

static Series series[CHART_MAX_SYMBOLS];
static int nextIndex = 0;
static unsigned long lastFetchAtMs = 0;

void chartBegin() {
  lastFetchAtMs = millis() - CHART_FETCH_INTERVAL_MS; // allow an immediate first fetch
}

int chartPointCount(int symbolIndex) {
  if (symbolIndex < 0 || symbolIndex >= CHART_MAX_SYMBOLS) return 0;
  return series[symbolIndex].count;
}

bool chartPoint(int symbolIndex, int i, float &closeOut) {
  if (symbolIndex < 0 || symbolIndex >= CHART_MAX_SYMBOLS) return false;
  const Series &s = series[symbolIndex];
  if (i < 0 || i >= s.count) return false;
  closeOut = s.closes[i];
  return true;
}

bool chartRange(int symbolIndex, float &loOut, float &hiOut, float &prevCloseOut) {
  if (symbolIndex < 0 || symbolIndex >= CHART_MAX_SYMBOLS) return false;
  const Series &s = series[symbolIndex];
  if (s.count < 2 || s.hi <= s.lo) return false;
  loOut = s.lo; hiOut = s.hi; prevCloseOut = s.prevClose;
  return true;
}

// Parses the Yahoo chart response into a symbol's series. The payload is
// about 3.6KB for 27 points. Nulls appear in thin trading and are skipped
// rather than plotted as zero.
static bool parseChart(const String &body, Series &out) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) return false;

  JsonObject result = doc["chart"]["result"][0];
  if (result.isNull()) return false;

  JsonArray closes = result["indicators"]["quote"][0]["close"];
  if (closes.isNull()) return false;

  float lo = 0, hi = 0;
  int n = 0;
  bool first = true;
  for (JsonVariant v : closes) {
    if (n >= CHART_MAX_POINTS) break;
    if (v.isNull()) continue;
    float c = v.as<float>();
    out.closes[n++] = c;
    if (first) { lo = hi = c; first = false; }
    else { if (c < lo) lo = c; if (c > hi) hi = c; }
  }
  if (n < 2) return false;

  float prev = result["meta"]["chartPreviousClose"] | 0.0f;
  if (prev > 0) { if (prev < lo) lo = prev; if (prev > hi) hi = prev; }

  out.count = n; out.lo = lo; out.hi = hi; out.prevClose = prev;
  return true;
}

// A failed fetch leaves the previous series untouched, so the panel keeps
// showing the last good chart rather than blanking.
static void fetchChart(int index) {
  String sym;
  float p, pc; bool valid;
  if (!tickerEntry(index, sym, p, pc, valid)) return;

  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  String url = "https://query1.finance.yahoo.com/v8/finance/chart/";
  url += sym;
  url += "?range=1d&interval=15m";

  if (!http.begin(client, url)) return;
  http.setUserAgent("ESP32HTTPClient"); // a browser UA gets rate limited

  if (http.GET() == 200) {
    Series parsed;
    if (parseChart(http.getString(), parsed)) {
      series[index] = parsed;
    }
  }
  http.end();
}

void chartPump() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastFetchAtMs < CHART_FETCH_INTERVAL_MS) return;
  lastFetchAtMs = millis();

  int n = tickerSymbolCount();
  if (n <= 0) return;
  if (nextIndex >= n) nextIndex = 0;
  fetchChart(nextIndex);
  nextIndex = (nextIndex + 1) % n;
}
```

- [ ] **Step 3: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles clean. Nothing calls the module yet; the linker may drop it, which is fine.

- [ ] **Step 4: Commit**

```bash
git add chart.h chart.cpp
git commit -m "Add chart module: Yahoo candle history

One symbol fetched every 30s, so eight refresh about every four minutes.
Stores closes only, which is what a line chart needs, plus the range and
previous close for scaling and the reference line.

A failed fetch leaves the previous series in place rather than blanking
the panel. The endpoint is undocumented, so every failure path is a
no-op rather than an error state."
```

---

## Task 4: Render the two chart panels

**Files:**
- Modify: `ui.h`
- Modify: `ui.cpp`

**Interfaces:**
- Consumes: `chartPointCount`, `chartPoint`, `chartRange` (Task 3); `tickerEntry`, `tickerHasApiKey` (existing).
- Produces:
  - `void uiDrawChartPanel(TFT_eSPI &tft, int slot, int symbolIndex);` - slot 0 is upper, 1 is lower.
  - `void uiClearChartPanels(TFT_eSPI &tft);`
  - Replaces `uiDrawTickerPage`, `uiTickerPageCount`, `uiClearTickerBand`, which are deleted.

- [ ] **Step 1: Replace the ticker declarations in `ui.h`**

Delete `uiDrawTickerPage`, `uiTickerPageCount` and `uiClearTickerBand`. Add:

```c
// Renders one symbol panel: the symbol and percent change on one line,
// the price below, then a line chart of the day. slot 0 is the upper
// panel, slot 1 the lower. Repaints only that panel's own strip.
void uiDrawChartPanel(TFT_eSPI &tft, int slot, int symbolIndex);

// Blanks both panel strips and the divider between them.
void uiClearChartPanels(TFT_eSPI &tft);
```

- [ ] **Step 2: Replace the band renderer in `ui.cpp`**

Delete `TICKER_BAND_TOP`, `TICKER_BAND_H`, `TICKER_PER_PAGE`, `uiClearTickerBand`, `uiTickerPageCount`, `drawTickerEntry` and `uiDrawTickerPage`. Keep `drawTrendArrow`. Add `#include "chart.h"` and:

```cpp
// Two stacked panels fill the lower two thirds. Each is 118px tall: a
// header line, a price line, then the chart.
static const int PANEL_TOP[2]   = { 84, 202 };
static const int PANEL_H        = 118;
static const int PANEL_HDR_DY   = 16;   // header baseline within the panel
static const int PANEL_PRICE_DY = 44;   // price baseline within the panel
static const int PANEL_CHART_DY = 56;   // chart top within the panel
static const int PANEL_CHART_H  = 58;

void uiClearChartPanels(TFT_eSPI &tft) {
  tft.fillRect(0, PANEL_TOP[0], TFT_HRES, PANEL_H * 2, TFT_BLACK);
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

  String sym; float price, pct; bool valid;
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
```

- [ ] **Step 3: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: fails, because `water-reminder.ino` still calls the deleted band functions. That is Task 5. Confirm the only errors are those call sites.

- [ ] **Step 4: Commit**

```bash
git add ui.h ui.cpp
git commit -m "Replace the price band with two chart panels

Each panel carries the symbol and percent change, the price, and a line
chart of the day with a dashed line at the previous close. Line rather
than candlestick: at 58px of chart height and 240px of width, candle
bodies would be about 5px wide with 1px wicks.

The loop still calls the deleted band functions, so this does not build
on its own. Task 5 rewires it."
```

---

## Task 5: Wire the panels into the loop

**Files:**
- Modify: `water-reminder.ino`

**Interfaces:**
- Consumes: everything from Tasks 3 and 4.

- [ ] **Step 1: Swap the includes and state**

Add `#include "chart.h"`. Replace the ticker page state block:

```cpp
// Chart panel state. Two symbols are shown at a time, advancing to the
// next pair every PANEL_PAGE_MS. Repainted only when the pair changes,
// which is what keeps the screen from flickering.
static const unsigned long PANEL_PAGE_MS = 5000;
static unsigned long lastPanelPageMs = 0;
static int panelPair = 0;
static bool panelsVisible = false;
```

- [ ] **Step 2: Start the chart module in `setup()`**

After `tickerBegin();`:

```cpp
  chartBegin();
```

- [ ] **Step 3: Replace the band block in `loop()`**

Replace the whole `marketOpen` block in the `STATE_IDLE` branch:

```cpp
    tickerPump(); // prices, Finnhub, market hours only
    chartPump();  // candles, Yahoo, at most one symbol per 30s

    int symbolCount = tickerSymbolCount();
    int pairCount = symbolCount > 0 ? (symbolCount + 1) / 2 : 0;

    if (pairCount == 0) {
      if (panelsVisible) { uiClearChartPanels(tft); panelsVisible = false; }
    } else if (!panelsVisible) {
      // Entering idle, or returning from an alert that wiped the screen.
      lastPanelPageMs = millis();
      uiDrawChartPanel(tft, 0, panelPair * 2);
      uiDrawChartPanel(tft, 1, panelPair * 2 + 1);
      panelsVisible = true;
    } else if (millis() - lastPanelPageMs >= PANEL_PAGE_MS) {
      lastPanelPageMs = millis();
      panelPair = (panelPair + 1) % pairCount;
      uiDrawChartPanel(tft, 0, panelPair * 2);
      uiDrawChartPanel(tft, 1, panelPair * 2 + 1);
    }
```

Note `uiDrawChartPanel` returns early when `tickerEntry` fails, so an odd final symbol leaves the lower panel blank without special-casing.

- [ ] **Step 4: Re-show the panels after an alert**

In the `STATE_ALERT` dismissal branch, replace `tickerBandVisible = false;` with:

```cpp
      panelsVisible = false; // the full repaint wiped the panels; let them redraw
```

- [ ] **Step 5: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles clean.

- [ ] **Step 6: DEFERRED to Task 7 - verify the panels**

Requires the device. To check in Task 7:

1. Two panels, each with symbol, percent, price and a line chart.
2. The pair advances every 5 seconds through all eight symbols.
3. The clock above does not flicker.
4. A reminder still fires, and after dismissing it the panels come back.

Charts render at any hour, since Yahoo keeps serving the last session. Live movement needs US market hours (14:30-21:00 Dublin), but presence of the charts does not.

- [ ] **Step 7: Commit**

```bash
git add water-reminder.ino
git commit -m "Wire the chart panels into the idle loop

Two symbols at a time, advancing every five seconds, repainted only when
the pair changes. The panels are re-shown after an alert, since
uiDrawIdleScreen clears the whole screen on dismissal."
```

---

## Task 6: Update the documentation

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Replace the price ticker section**

```markdown
## Price charts

The lower two thirds of the screen show two stocks at a time from AAPL,
MSFT, GOOGL, AMZN, NVDA, META, TSLA and NFLX, advancing every 5 seconds
so the full set comes round every 20. Each panel shows the symbol, the
percent change, the price, and a line chart of the day's trading with a
dashed line at the previous close.

Prices come from Finnhub and need a free API key, entered on the device's
setup page. Charts come from Yahoo Finance and need no key. If the chart
data is unavailable the panels still show prices.
```

Update the opening paragraph to say portrait and charts rather than a landscape band.

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "Document the portrait layout and price charts"
```

---

## Task 7: The connected session

Everything above is built and compiling but has never run. This task is done with the device plugged in, in one sitting, with the owner watching the panel.

**Files:**
- Modify: `water-reminder.ino` (only if the rotation is wrong)
- Modify: `ui.cpp` (layout nudges)

**Interfaces:**
- Consumes: Tasks 1-6.
- Produces: a working portrait device.

- [ ] **Step 1: Repartition to huge_app**

Do this FIRST, before any verification, because it erases NVS and the owner has to re-enter credentials once. Doing it first means one re-entry, not two.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 --board-options PartitionScheme=huge_app .
arduino-cli upload -p /dev/cu.usbserial-120 --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app --board-options UploadSpeed=115200 .
```

Expected: usage falls from about 88% to about 37%, with roughly 1.9 MB free.

NVS is erased, so the device will open its setup portal. The owner rejoins `WaterReminder-Setup` and re-enters WiFi credentials and the Finnhub API key.

Record the flag in the README so future flashes use it; a normal flash without it would not fit the new layout.

- [ ] **Step 2: Rotation - which way up?**

With the board upright, connector at the top: is the text the right way up?

If upside down, change BOTH `setRotation(0)` calls in `water-reminder.ino` to `setRotation(2)`, recompile with the huge_app flag, reflash.

- [ ] **Step 3: Idle layout**

Check the clock is not clipped at the top, is horizontally centred, and the AM/PM sits on the digits' baseline. Nudge `IDLE_CLOCK_Y` in `ui.cpp` if not; everything below derives from it.

- [ ] **Step 4: Panels**

Check the four points listed in Task 5 Step 6. Likely adjustments: `PANEL_TOP`, `PANEL_H`, `PANEL_CHART_H`, and the `PANEL_*_DY` offsets.

- [ ] **Step 5: Reminder still works**

The device's actual job. Confirm an alert fires, flashes, and dismisses on tap and on timeout, and that the panels return afterwards.

Use `DEBUG_FAST_SCHEDULE` in `config.h` to avoid waiting for a real half-hour mark. Turn it back off and reflash before finishing.

- [ ] **Step 6: Commit whatever the panel taught us**

```bash
git add -A
git commit -m "Tune the portrait layout on hardware

Values that were arithmetic in Tasks 1-6, corrected against the panel."
```
