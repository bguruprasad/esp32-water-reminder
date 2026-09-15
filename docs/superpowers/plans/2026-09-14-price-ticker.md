# Share Price Ticker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a share-price band along the bottom of the idle screen showing AAPL, MSFT, GOOGL, AMZN and NVDA, fetched from Finnhub and hidden outside US market hours.

**Architecture:** A new self-contained `ticker.*` module owns fetching, market-hours logic and price state; it knows nothing about the clock. `ui.*` gains a band renderer that repaints only its own strip. `wifi_setup.*` gains an API-key field on the existing captive portal. `water-reminder.ino` wires the non-blocking fetch pump and band repaint into the existing `loop()`.

**Tech Stack:** Arduino on ESP32 (core 3.3.11, FQBN `esp32:esp32:esp32`), `HTTPClient` + `NetworkClientSecure` (in core), ArduinoJson 7.4.3, `Preferences` (NVS), `TFT_eSPI`.

**Spec:** [docs/superpowers/specs/2026-09-14-water-reminder-design.md](../specs/2026-09-14-water-reminder-design.md) — see the "Addendum: share price ticker" section.

## Global Constraints

- Symbols, in display order: `AAPL`, `MSFT`, `GOOGL`, `AMZN`, `NVDA`.
- One symbol fetched per 12 seconds, rotating — five symbols refresh each minute, 5 calls/min against Finnhub's ~60/min free limit.
- Endpoint: `https://finnhub.io/api/v1/quote?symbol=<SYM>&token=<KEY>`.
- TLS: `client.setInsecure()`. Do NOT bake in a CA root certificate.
- The API key is entered on the device setup page and stored in NVS. It must NEVER appear in source, in a committed file, or in a log line. When printing diagnostics, print only whether a key is present, never its value.
- Fetching is synchronous and CAN briefly stall the UI loop. This was reviewed and consciously accepted (see the amendment below) — it is not a defect to re-flag.
- Band occupies y=200-235. Nothing above it moves: `IDLE_CLOCK_Y`, `IDLE_DIVIDER_Y`, `IDLE_DAY_Y`, `IDLE_PILL_Y`, `IDLE_PILL_H` keep their current values.
- The band repaints only its own strip (`fillRect` over y=200-235), never `fillScreen` — that discipline is what removed the screen flicker.
- Market hours: 09:30-16:00 US Eastern, Mon-Fri. Outside them the band is hidden AND fetching stops.
- ET must be derived arithmetically inside `ticker.cpp`. Do NOT call `configTzTime()` or `setenv("TZ",...)` — one global timezone is already set to `Europe/Dublin` and the clock depends on it.
- Board: FQBN `esp32:esp32:esp32`, port `/dev/cu.usbserial-120`, upload speed `115200`.
- Commit messages end with: `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`

## File Structure

| File | Responsibility |
|---|---|
| `ticker.h` / `ticker.cpp` (new) | Symbol list, price state, non-blocking fetch rotation, JSON parsing, US market-hours test. No display code. |
| `wifi_setup.cpp` (modify) | Third form field for the API key; save to NVS; expose a getter. |
| `wifi_setup.h` (modify) | Declare the key getter. |
| `ui.h` / `ui.cpp` (modify) | `uiDrawTickerBand()` — renders the band strip from ticker state. |
| `water-reminder.ino` (modify) | Call the fetch pump and band repaint from `loop()`. |

**Two integration hazards this plan must handle:**

1. `uiDrawIdleScreen()` begins with `fillScreen(TFT_BLACK)`, and `loop()` calls it on every alert dismissal. That erases the band, so the band must be repainted after any full idle redraw (Task 6).
2. `wifiSetupStartPortal()` never returns — it ends in `ESP.restart()` inside a `while(true)`. The API key must therefore be saved in that same block alongside SSID/password (Task 2); there is no post-portal code path.

---

## Execution order

Actual order run: **Task 2 → Task 1 → 3 → 4 → 5 → 6.** Task 2 went first
because Task 1 needed the API key to exist in NVS, and nothing wrote it
there until Task 2 built the setup-portal field. Task 2 is COMPLETE and
hardware-verified; the key is in NVS.

Task numbering is kept as-is so the task briefs and this plan's internal
cross-references stay stable.

## Amendment: synchronous fetch, bounded stall accepted (2026-09-15)

The original Global Constraint asserted that fetching must never stall the
UI loop or delay a reminder. Task 3's review demonstrated that the
implementation does not satisfy that, and the owner has consciously
accepted the behaviour rather than engineering around it.

What actually happens: `fetchSymbol()` performs a synchronous HTTPS GET
from `tickerPump()`, which runs inside `loop()`. While a request is in
flight the UI is frozen. Worst case is now bounded at roughly 5 seconds by
`setConnectTimeout(5000)` (the connect/TLS phase, previously uncapped) plus
`setTimeout(5000)` (the read phase). It can occur at most once per 12
seconds, and only during US market hours.

Consequence, stated plainly: a water reminder falling due mid-fetch is
delayed by up to that bound. For a decorative price band this was judged an
acceptable trade against the complexity of moving fetching onto the second
core with cross-core shared state.

**This is settled. Do not re-flag the synchronous fetch as a defect, and do
not add a FreeRTOS task or async client to "fix" it.** If it ever becomes a
nuisance in practice, the escape hatch is a background task on core 0.

## Amendment: no serial logging (2026-09-15)

Serial reads from this Mac proved unreliable across many attempts, and
the decision is to stop depending on them: **diagnostics render to the
TFT, not to the serial port.**

Consequences for the tasks below:

- **Task 1 is replaced** (see its section): the Finnhub JSON contract is
  confirmed by running one `curl` from the host, not by an on-device
  probe read over serial. The response shape is a property of the API,
  identical whichever machine fetches it, so no firmware is needed.
- **Task 3**: the `Serial.printf` diagnostic lines in `ticker.cpp` are
  dropped. Fetch failures surface on screen via the band's own states
  (`--` for a symbol with no valid quote, `Ticker: no API key` when no
  key is stored). Do NOT add serial logging.
- The existing `Serial.print` calls elsewhere in the project are left
  alone; this amendment only governs new ticker code.

---

## Task 1: Confirm the Finnhub JSON contract (host-side, no firmware)

The spec records that the success payload's field names are **unverified** — no API key existed at planning time, and nothing may be built on an assumed JSON contract.

**This task writes no code and touches no files.** It was originally an on-device probe read over the serial port; serial reading proved unreliable on this machine and was abandoned (see the "no serial logging" amendment). The response shape is a property of the Finnhub API, identical whichever machine fetches it, so a single host-side request settles it with no firmware, no flashing, and no serial.

**Files:** none.

**Interfaces:**
- Consumes: nothing.
- Produces: the confirmed field mapping, written into Task 3's `parseQuote()` before Task 3 is implemented.

- [ ] **Step 1: The owner runs one request from the host**

The API key must NOT be pasted into the agent conversation, a file, or a command the agent runs. The owner runs this themselves, substituting their key:

```bash
curl -s "https://finnhub.io/api/v1/quote?symbol=AAPL&token=YOUR_KEY_HERE"
```

and reports back **only the JSON response**, never the command line — the response body carries no secret, the URL does.

- [x] **Step 2: Record the mapping into Task 3** — DONE, contract confirmed

Observed response (AAPL, 2026-09-15):

```json
{"c":333.08,"d":0.81,"dp":0.2438,"h":335.5,"l":331.34,"o":334.79,"pc":332.27,"t":1789416000}
```

Confirmed, and it matches the documented shape exactly:

- `c` = current price. Present, a JSON **number**.
- `dp` = percent change. Present, a JSON **number**, and already expressed
  as a percentage (`0.2438` means 0.24%), NOT a 0-1 fraction. So Task 4's
  `"%.2f%%"` formatting is correct as written — do **not** multiply by 100.
- Both are unquoted numbers, so `doc["c"].is<float>()` in `parseQuote()`
  succeeds. (Had they been quoted strings, that check would have failed
  and every quote would have been silently discarded.)

**Task 3's `parseQuote()` needs no changes — implement it as written.**

- [ ] **Step 3: No commit**

Nothing to commit — this task produces a recorded finding, not a code change.

---

## Task 2: API key field on the setup portal

**Files:**
- Modify: `wifi_setup.cpp`
- Modify: `wifi_setup.h`

**Interfaces:**
- Consumes: the existing portal in `wifi_setup.cpp`.
- Produces: `String wifiSetupApiKey();` — returns the stored Finnhub key, or `""` if none. Tasks 3 and 5 call it.

- [ ] **Step 1: Declare the getter in `wifi_setup.h`**

Add before `#endif`:

```c
// Returns the Finnhub API key saved via the setup portal, or "" if none
// has been entered. The key lives only in NVS — never in source control.
String wifiSetupApiKey();
```

- [ ] **Step 2: Add the NVS key name and submitted-value holder in `wifi_setup.cpp`**

After the existing `NVS_KEY_PASS` line:

```cpp
static const char *NVS_KEY_APIKEY = "apikey";
```

After `static String submittedPass;`:

```cpp
static String submittedApiKey;
```

- [ ] **Step 3: Add the field to the form**

Replace the `FORM_HTML` definition with:

```cpp
static const char *FORM_HTML =
  "<!DOCTYPE html><html><head><meta name='viewport' "
  "content='width=device-width,initial-scale=1'>"
  "<title>Water Reminder Setup</title></head><body>"
  "<h2>Water Reminder: Setup</h2>"
  "<form method='POST' action='/save'>"
  "SSID:<br><input name='ssid' maxlength='32'><br>"
  "Password:<br><input name='pass' type='password' maxlength='64'><br><br>"
  "Finnhub API key (optional):<br>"
  "<input name='apikey' type='password' maxlength='64'><br>"
  "<small>Leave blank to run without the price ticker.</small><br><br>"
  "<input type='submit' value='Save and Connect'>"
  "</form></body></html>";
```

- [ ] **Step 4: Capture it on submit**

In `handleSave()`, after the `submittedPass` line:

```cpp
  submittedApiKey = webServer.arg("apikey");
```

- [ ] **Step 5: Persist it**

In `wifiSetupStartPortal()`'s save block — the one ending in `ESP.restart()`, since the function never returns — after the `putString(NVS_KEY_PASS, ...)` line:

```cpp
      prefs.putString(NVS_KEY_APIKEY, submittedApiKey);
```

- [ ] **Step 6: Implement the getter**

Add at the end of `wifi_setup.cpp`:

```cpp
String wifiSetupApiKey() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true); // read-only
  String key = prefs.getString(NVS_KEY_APIKEY, "");
  prefs.end();
  return key;
}
```

- [ ] **Step 7: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles clean.

- [ ] **Step 8: Flash and verify on hardware**

Because the device already has saved WiFi credentials, the portal will NOT appear on boot. To reach it, temporarily clear NVS or connect while the router is off so the connect times out and the portal opens. Simplest: power the board with the router unreachable, or briefly add `prefs.clear()` before the read in `wifiSetupConnect()` and flash once.

Verify with the user: the setup page shows three fields; entering a key saves it; after reboot the device reconnects normally. The key must NOT be printed to serial — check that no log line contains it.

- [ ] **Step 9: Commit**

```bash
git add wifi_setup.h wifi_setup.cpp
git commit -m "Add Finnhub API key field to the setup portal

Third field on the existing captive portal, saved to NVS beside the
WiFi credentials so no secret ever enters git. Optional: left blank,
the device runs without the ticker.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 3: Ticker module — state, fetching, market hours

**Files:**
- Create: `ticker.h`
- Create: `ticker.cpp`

**Interfaces:**
- Consumes: `wifiSetupApiKey()` (Task 2).
- Produces:
  - `void tickerBegin();` — call once in `setup()`.
  - `void tickerPump();` — call every `loop()`; non-blocking, fetches at most one symbol per 12s and only during market hours.
  - `bool tickerIsMarketOpen(const struct tm &nowLocalDublin);` — true during 09:30-16:00 ET, Mon-Fri.
  - `int tickerSymbolCount();`
  - `bool tickerEntry(int index, String &symbolOut, float &priceOut, float &pctOut, bool &validOut);` — reads one symbol's latest state for rendering.
  - `bool tickerHasApiKey();`

- [ ] **Step 1: Write `ticker.h`**

```c
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
```

- [ ] **Step 2: Write `ticker.cpp`**

Replace the two field names in `parseQuote()` if Task 1 observed different ones.

```cpp
#include "ticker.h"
#include "wifi_setup.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>

static const char *SYMBOLS[] = {"AAPL", "MSFT", "GOOGL", "AMZN", "NVDA"};
static const int SYMBOL_COUNT = 5;
static const unsigned long TICKER_FETCH_INTERVAL_MS = 12000;

struct Quote {
  float price = 0.0f;
  float pct = 0.0f;
  bool valid = false; // false until a first successful fetch
};

static Quote quotes[SYMBOL_COUNT];
static int nextSymbolIndex = 0;
static unsigned long lastFetchAtMs = 0;
static String apiKey = "";

void tickerBegin() {
  apiKey = wifiSetupApiKey();
  // No logging — whether a key is present is visible on screen via the
  // band's "Ticker: no API key" state.
  lastFetchAtMs = millis() - TICKER_FETCH_INTERVAL_MS; // allow an immediate first fetch
}

bool tickerHasApiKey() { return apiKey.length() > 0; }
int tickerSymbolCount() { return SYMBOL_COUNT; }

bool tickerEntry(int index, String &symbolOut, float &priceOut,
                 float &pctOut, bool &validOut) {
  if (index < 0 || index >= SYMBOL_COUNT) return false;
  symbolOut = SYMBOLS[index];
  priceOut = quotes[index].price;
  pctOut = quotes[index].pct;
  validOut = quotes[index].valid;
  return true;
}

// --- US Eastern time, derived arithmetically ---------------------------
// The global TZ is Europe/Dublin and the clock depends on it, so ET is
// computed here rather than by changing the process timezone.
//
// US DST: second Sunday of March to first Sunday of November.
// Dublin: UTC+0 winter / UTC+1 summer (EU rules, last Sundays).
// Rather than chain two DST rules, convert Dublin local -> UTC using the
// tm_isdst the system already resolved, then UTC -> ET.

// VERIFIED against 14 boundary cases on the host before this plan was
// finalised, including both 2026 and 2027 transitions: 2026-03-07/08/09,
// 2026-03-01, 2026-10-31, 2026-11-01, 2027-03-13/14, 2027-11-06/07, plus
// mid-season months. All correct — use as written, no need to re-derive.
//
// Derivation: for any date, the day-of-month of the most recent Sunday
// is tm_mday - tm_wday. The first Sunday of the month is therefore
// ((tm_mday - tm_wday - 1) % 7) + 1.
static bool usesUsDst(const struct tm &utc) {
  int month = utc.tm_mon + 1; // tm_mon is 0-based
  if (month < 3 || month > 11) return false;
  if (month > 3 && month < 11) return true;

  int firstSundayDom = ((utc.tm_mday - utc.tm_wday - 1) % 7 + 7) % 7 + 1;
  if (month == 3) {
    return utc.tm_mday >= firstSundayDom + 7; // second Sunday onward
  }
  return utc.tm_mday < firstSundayDom; // November: before the first Sunday
}

bool tickerIsMarketOpen(const struct tm &nowLocalDublin) {
  // Dublin local -> UTC. tm_isdst carries whether IST is in effect.
  struct tm copy = nowLocalDublin;
  time_t asEpoch = mktime(&copy); // interprets as local (Dublin) time
  struct tm utc;
  gmtime_r(&asEpoch, &utc);

  int etOffsetHours = usesUsDst(utc) ? -4 : -5;
  time_t etEpoch = asEpoch + (time_t)etOffsetHours * 3600;
  struct tm et;
  gmtime_r(&etEpoch, &et); // gmtime on a shifted epoch yields ET wall time

  if (et.tm_wday < 1 || et.tm_wday > 5) return false; // Mon-Fri only

  int minutes = et.tm_hour * 60 + et.tm_min;
  const int openMinutes = 9 * 60 + 30;  // 09:30
  const int closeMinutes = 16 * 60;     // 16:00
  return minutes >= openMinutes && minutes < closeMinutes;
}

// --- Fetching -----------------------------------------------------------

static bool parseQuote(const String &body, float &priceOut, float &pctOut) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  if (!doc["c"].is<float>()) return false; // "c" = current price
  priceOut = doc["c"].as<float>();
  pctOut = doc["dp"].is<float>() ? doc["dp"].as<float>() : 0.0f; // "dp" = percent change
  return true;
}

// No serial logging here — see the "no serial logging" amendment. A
// failed fetch simply leaves the symbol's previous value in place
// (quotes[index].valid stays as it was), which the band renders as the
// last good price, or as "--" if there has never been one.
static void fetchSymbol(int index) {
  NetworkClientSecure client;
  client.setInsecure(); // see spec: deliberate, Cloudflare cert rotation
  HTTPClient http;
  http.setTimeout(5000);

  String url = "https://finnhub.io/api/v1/quote?symbol=";
  url += SYMBOLS[index];
  url += "&token=";
  url += apiKey;

  if (!http.begin(client, url)) return;

  if (http.GET() == 200) {
    float price, pct;
    if (parseQuote(http.getString(), price, pct)) {
      quotes[index].price = price;
      quotes[index].pct = pct;
      quotes[index].valid = true;
    }
  }
  http.end();
}

void tickerPump() {
  if (apiKey.length() == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;

  time_t now = time(nullptr);
  struct tm nowLocal;
  localtime_r(&now, &nowLocal);
  if (!tickerIsMarketOpen(nowLocal)) return; // no fetching while shut

  if (millis() - lastFetchAtMs < TICKER_FETCH_INTERVAL_MS) return;
  lastFetchAtMs = millis();

  fetchSymbol(nextSymbolIndex);
  nextSymbolIndex = (nextSymbolIndex + 1) % SYMBOL_COUNT;
}
```

- [ ] **Step 3: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles clean. (Nothing calls the module yet; the include in Task 5 wires it up. If the linker drops it, that is fine at this stage.)

- [ ] **Step 4: Commit**

```bash
git add ticker.h ticker.cpp
git commit -m "Add ticker module: fetch rotation, parsing, market hours

One symbol fetched per 12s so five refresh each minute, well inside
Finnhub's free limit. US Eastern is derived arithmetically because the
global timezone belongs to the clock. Fetching stops outside market
hours. The API key is never logged.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 4: Render the band

**Files:**
- Modify: `ui.h`
- Modify: `ui.cpp`

**Interfaces:**
- Consumes: `tickerSymbolCount()`, `tickerEntry()`, `tickerHasApiKey()` (Task 3).
- Produces: `void uiDrawTickerBand(TFT_eSPI &tft, int scrollOffsetPx);` and `void uiClearTickerBand(TFT_eSPI &tft);`

- [ ] **Step 1: Declare in `ui.h`**

Add before `#endif`:

```c
// Renders the share-price band along the bottom strip (y=200-235),
// scrolled left by scrollOffsetPx. Repaints only its own strip, never
// the whole screen, so it does not reintroduce flicker.
void uiDrawTickerBand(TFT_eSPI &tft, int scrollOffsetPx);

// Blanks the band's strip — used when the market is shut.
void uiClearTickerBand(TFT_eSPI &tft);
```

- [ ] **Step 2: Implement in `ui.cpp`**

Add `#include "ticker.h"` at the top, then append:

```cpp
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

void uiDrawTickerBand(TFT_eSPI &tft, int scrollOffsetPx) {
  uiClearTickerBand(tft);

  int textY = TICKER_BAND_TOP + TICKER_BAND_H / 2;
  tft.setTextDatum(ML_DATUM);
  tft.setFreeFont(NULL);

  if (!tickerHasApiKey()) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Ticker: no API key", 8, textY, 2);
    tft.setTextDatum(MC_DATUM);
    return;
  }

  const int entryGap = 28;
  int x = -scrollOffsetPx;

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

    int w = tft.textWidth(buf, 2);

    // Skip entries entirely off-screen; keeps the loop cheap.
    if (x + w > 0 && x < TFT_HRES) {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(buf, x, textY, 2);

      if (valid) {
        bool up = pct >= 0.0f;
        uint16_t c = up ? TFT_GREEN : TFT_RED;
        drawTrendArrow(tft, x + w + 8, textY, up, c);

        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), "%.2f%%", pct < 0 ? -pct : pct);
        tft.setTextColor(c, TFT_BLACK);
        tft.drawString(pctBuf, x + w + 16, textY, 2);
        w += 16 + tft.textWidth(pctBuf, 2);
      }
    }

    x += w + entryGap;
  }

  tft.setTextDatum(MC_DATUM); // restore the datum the other screens expect
}
```

- [ ] **Step 3: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles clean.

- [ ] **Step 4: Commit**

```bash
git add ui.h ui.cpp
git commit -m "Add ticker band renderer

Repaints only the bottom strip (y=200-235), leaving the tuned idle
layout above untouched and preserving the partial-redraw discipline.
Trend arrows are shape-drawn, as there is no emoji font.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 5: Wire it into the loop

**Files:**
- Modify: `water-reminder.ino`

**Interfaces:**
- Consumes: everything from Tasks 2-4.
- Produces: a running ticker.

- [ ] **Step 1: (nothing to remove)**

Task 1 was replaced by a host-side `curl` and adds no code to `water-reminder.ino`, so there is no diagnostic block to delete here. Confirm the file has no `[ticker-probe]` text and no stray `#include <HTTPClient.h>` / `#include <NetworkClientSecure.h>` at the sketch level, then move on:

```bash
grep -n "ticker-probe\|HTTPClient\|NetworkClientSecure" water-reminder.ino || echo "clean, nothing to remove"
```

- [ ] **Step 2: Include the module and add scroll state**

Add `#include "ticker.h"` with the other project includes. Then after the alert-state block:

```cpp
// Ticker band scroll state. The band advances a few pixels per frame
// while the market is open; it is hidden entirely when shut.
static const unsigned long TICKER_FRAME_MS = 40; // ~25fps
static const int TICKER_SCROLL_STEP_PX = 2;
static unsigned long lastTickerFrameMs = 0;
static int tickerScrollPx = 0;
static bool tickerBandVisible = false;
```

- [ ] **Step 3: Start the module in `setup()`**

At the end of `setup()`, after `lastFiredMarkInitialized = true;`:

```cpp
  tickerBegin();
```

- [ ] **Step 4: Pump and render from `loop()`**

Inside the `if (appState == STATE_IDLE)` branch, after the `uiUpdateIdleScreen(...)` call and before the mark-due check:

```cpp
    tickerPump(); // non-blocking; at most one fetch per 12s, market hours only

    bool marketOpen = tickerIsMarketOpen(nowLocal);
    if (!marketOpen) {
      if (tickerBandVisible) { // hide once, not every tick
        uiClearTickerBand(tft);
        tickerBandVisible = false;
      }
    } else if (millis() - lastTickerFrameMs >= TICKER_FRAME_MS) {
      lastTickerFrameMs = millis();
      tickerScrollPx += TICKER_SCROLL_STEP_PX;
      // Wrap against the real content width that the renderer reports,
      // not a guessed constant. An oversized constant leaves the band
      // blank for thousands of pixels of travel and then snaps back
      // visibly; wrapping on the actual width loops it continuously.
      int contentWidth = uiDrawTickerBand(tft, tickerScrollPx);
      if (contentWidth > 0 && tickerScrollPx >= contentWidth) {
        tickerScrollPx = 0;
      }
      tickerBandVisible = true;
    }
```

- [ ] **Step 5: Repaint the band after an alert dismissal**

`uiDrawIdleScreen()` starts with `fillScreen(TFT_BLACK)`, which erases the band. In the `STATE_ALERT` dismissal branch, immediately after the existing `uiDrawIdleScreen(...)` call:

```cpp
      tickerBandVisible = false; // full repaint wiped the band; let it redraw
```

- [ ] **Step 6: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles clean.

- [ ] **Step 7: Flash**

```bash
arduino-cli upload -p /dev/cu.usbserial-120 --fqbn esp32:esp32:esp32 --board-options UploadSpeed=115200 .
```

- [ ] **Step 8: Verify on hardware with the user**

Confirm, during US market hours (14:30-21:00 Dublin time):
1. The band scrolls along the bottom with the five symbols and live prices.
2. Green up / red down arrows match the percent signs.
3. The clock above still does NOT flicker.
4. Reminders still fire, and after dismissing one the band comes back.

Outside market hours confirm the band is hidden and the rest of the screen is unchanged.

- [ ] **Step 9: Commit**

```bash
git add water-reminder.ino
git commit -m "Wire the price ticker into the idle loop

Pumps the non-blocking fetch and scrolls the band while the US market
is open, hiding it otherwise. The band is re-shown after an alert
dismissal, since the full idle repaint clears the whole screen.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 6: README

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Document the ticker**

Add a section after the existing first-time-setup steps:

```markdown
## Price ticker

The bottom of the idle screen shows a scrolling share-price band
(AAPL, MSFT, GOOGL, AMZN, NVDA), hidden outside US market hours
(09:30-16:00 ET, Mon-Fri).

It needs a free Finnhub API key from https://finnhub.io/register. Enter
it in the third field of the device's setup page, alongside your WiFi
details. The key is stored on the device and never appears in this
repository — leave the field blank to run without the ticker.

Prices refresh one symbol every 12 seconds, so all five update each
minute — comfortably inside Finnhub's free rate limit.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "Document the price ticker and its API key setup

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```
