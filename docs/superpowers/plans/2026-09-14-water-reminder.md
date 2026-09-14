# Water Reminder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an ESP32 + Freenove 2.8" touch TFT device that shows a full-screen "drink water" reminder every 30 minutes, Mon–Fri, 09:00–18:00 Dublin time, dismissed by tapping the screen.

**Architecture:** Single Arduino sketch directory (`water-reminder/water-reminder.ino`) split into small header+source pairs by responsibility: WiFi captive-portal provisioning, NTP time sync, a pure schedule-engine (no hardware deps, easiest to reason about in isolation), and UI (idle/alert screens + touch). `setup()`/`loop()` in the main `.ino` just wires these together. No unit-test framework — each task ends in a compile + flash + on-device behavior check, per the spec's testing approach.

**Tech Stack:** Arduino framework on ESP32 (esp32:esp32 core 3.3.11, via arduino-cli, FQBN `esp32:esp32:esp32`), `TFT_eSPI` (configured for FNK0114B_2P8_240x320_ST7789), `TFT_Touch`, and ESP32-core built-ins `WiFi`, `DNSServer`, `WebServer`, `Preferences`, `time.h`. No new library installs.

**Spec:** [docs/superpowers/specs/2026-09-14-water-reminder-design.md](../specs/2026-09-14-water-reminder-design.md)

## Global Constraints

- Schedule window: Monday–Friday, 09:00–18:00 local time, reminder every 30 minutes on the half-hour (09:00, 09:30, ..., 17:30 — never 18:00 itself).
- Timezone: `Europe/Dublin`, POSIX TZ string `GMT0IST,M3.5.0/1,M10.5.0` (auto DST).
- No WiFi credentials ever committed to git — provisioning is via on-device captive portal only (SSID `WaterReminder-Setup`), credentials persisted in NVS via `Preferences`.
- No new library dependencies beyond what's already installed: `TFT_eSPI`, `TFT_Touch`, and the ESP32 Arduino core (`WiFi`, `DNSServer`, `WebServer`, `Preferences`, `time.h`).
- No audio/beep in this build — full-screen tap-to-dismiss is the only alert channel (deferred per spec Non-goals: no speaker hardware yet).
- Board: FQBN `esp32:esp32:esp32`, upload port `/dev/cu.usbserial-120`, upload speed `115200` (921600 was unreliable on this device — see prior session).
- Display driver selection already done at the library level: `TFT_eSPI/User_Setup_Select.h` has `FNK0114B_2P8_240x320_ST7789` active. Do not change this.
- Touch pins (from Freenove reference sketches): `DOUT=39, DIN=32, DCS=33, DCLK=25`. Screen resolution landscape: `HRES=320, VRES=240`, `tft.setRotation(1)`, `touch.setRotation(1)`.

---

## Task 1: Project scaffold + compile/flash smoke test

**Files:**
- Create: `water-reminder.ino`
- Create: `config.h`

**Interfaces:**
- Produces: `config.h` defines `TFT_HRES`, `TFT_VRES` (320/240), `TOUCH_DOUT`, `TOUCH_DIN`, `TOUCH_DCS`, `TOUCH_DCLK` (39/32/33/25), and `#define DEBUG_FAST_SCHEDULE` commented out by default. Later tasks include this header for shared constants.

- [ ] **Step 1: Write `config.h` with pin/resolution constants**

```c
#ifndef CONFIG_H
#define CONFIG_H

// Display (FNK0114B 2.8" ST7789, landscape)
#define TFT_HRES 320
#define TFT_VRES 240

// Touch controller pins (from Freenove reference sketches)
#define TOUCH_DOUT 39  // Data out (T_DO)
#define TOUCH_DIN  32  // Data in (T_DIN)
#define TOUCH_DCS  33  // Chip select (T_CS)
#define TOUCH_DCLK 25  // Clock (T_CLK)

// Uncomment to compress the 30-min schedule into 30-sec ticks for fast
// end-to-end testing. Must be OFF for real use.
//#define DEBUG_FAST_SCHEDULE

#endif
```

- [ ] **Step 2: Write minimal `water-reminder.ino` that initializes the display and touch, and prints a heartbeat**

```cpp
#include "config.h"
#include <TFT_eSPI.h>
#include <TFT_Touch.h>

TFT_eSPI tft = TFT_eSPI();
TFT_Touch touch = TFT_Touch(TOUCH_DCS, TOUCH_DCLK, TOUCH_DIN, TOUCH_DOUT);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("water-reminder: boot");

  tft.init();
  tft.setRotation(1);
  touch.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("water-reminder scaffold OK", TFT_HRES / 2, TFT_VRES / 2, 2);
}

void loop() {
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > 2000) {
    lastBeat = millis();
    Serial.println("heartbeat");
  }
}
```

- [ ] **Step 3: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles with no errors, reports flash/RAM usage.

- [ ] **Step 4: Flash and verify on device**

Run: `arduino-cli upload -p /dev/cu.usbserial-120 --fqbn esp32:esp32:esp32 --board-options UploadSpeed=115200 .`
Expected: upload succeeds, all partitions verified.
Then check the physical screen: it should show "water-reminder scaffold OK" centered on a black background. Confirm this visually (ask the user to look, or note it needs visual confirmation).

- [ ] **Step 5: Commit**

```bash
git add water-reminder.ino config.h
git commit -m "Add project scaffold: display+touch init smoke test

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

---

## Task 2: WiFi captive portal provisioning

**Files:**
- Create: `wifi_setup.h`
- Create: `wifi_setup.cpp`
- Modify: `water-reminder.ino`

**Interfaces:**
- Consumes: nothing from other tasks (self-contained; only needs `Serial` for logging).
- Produces:
  - `bool wifiSetupConnect(unsigned long connectTimeoutMs = 15000);` — tries stored NVS credentials, returns `true` if connected (STA mode, `WiFi.status() == WL_CONNECTED`) within the timeout, `false` otherwise (does NOT itself start the portal).
  - `void wifiSetupStartPortal();` — starts AP mode (`WaterReminder-Setup`), DNS redirect, and a blocking loop that serves the credential form via `WebServer` until the user submits, then saves to NVS and calls `ESP.restart()`. Never returns normally.
  - Later tasks call `wifiSetupConnect()` once in `setup()`; if it returns `false`, call `wifiSetupStartPortal()`.

- [ ] **Step 1: Write `wifi_setup.h`**

```c
#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include <Arduino.h>

// Tries to connect using credentials saved in NVS (namespace "wifi").
// Returns true if connected within connectTimeoutMs, false if no saved
// credentials or the connection attempt failed/timed out.
bool wifiSetupConnect(unsigned long connectTimeoutMs = 15000);

// Starts a "WaterReminder-Setup" access point + captive portal, serves a
// credential form, saves submitted credentials to NVS, then reboots.
// Blocks forever (never returns) — call only when wifiSetupConnect()
// returned false.
void wifiSetupStartPortal();

#endif
```

- [ ] **Step 2: Write `wifi_setup.cpp`**

```cpp
#include "wifi_setup.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>

static const char *NVS_NAMESPACE = "wifi";
static const char *NVS_KEY_SSID = "ssid";
static const char *NVS_KEY_PASS = "pass";
static const char *AP_SSID = "WaterReminder-Setup";
static const byte DNS_PORT = 53;

static DNSServer dnsServer;
static WebServer webServer(80);
static bool credentialsSaved = false;
static String submittedSsid;
static String submittedPass;

static const char *FORM_HTML =
  "<!DOCTYPE html><html><head><meta name='viewport' "
  "content='width=device-width,initial-scale=1'>"
  "<title>Water Reminder WiFi Setup</title></head><body>"
  "<h2>Water Reminder: WiFi Setup</h2>"
  "<form method='POST' action='/save'>"
  "SSID:<br><input name='ssid' maxlength='32'><br>"
  "Password:<br><input name='pass' type='password' maxlength='64'><br><br>"
  "<input type='submit' value='Save and Connect'>"
  "</form></body></html>";

static void handleRoot() {
  webServer.send(200, "text/html", FORM_HTML);
}

static void handleSave() {
  submittedSsid = webServer.arg("ssid");
  submittedPass = webServer.arg("pass");
  credentialsSaved = true;
  webServer.send(200, "text/html",
    "<html><body><h2>Saved. Rebooting...</h2></body></html>");
}

static void handleNotFound() {
  // Captive portal: redirect everything else to the form too.
  handleRoot();
}

bool wifiSetupConnect(unsigned long connectTimeoutMs) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true); // read-only
  String ssid = prefs.getString(NVS_KEY_SSID, "");
  String pass = prefs.getString(NVS_KEY_PASS, "");
  prefs.end();

  if (ssid.length() == 0) {
    Serial.println("wifi: no saved credentials");
    return false;
  }

  Serial.print("wifi: connecting to saved SSID: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < connectTimeoutMs) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("wifi: connected, IP=");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("wifi: connect failed/timed out");
  WiFi.disconnect(true);
  return false;
}

void wifiSetupStartPortal() {
  Serial.println("wifi: starting captive portal AP: " + String(AP_SSID));

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("wifi: AP IP=");
  Serial.println(apIP);

  dnsServer.start(DNS_PORT, "*", apIP);

  webServer.on("/", handleRoot);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.onNotFound(handleNotFound);
  webServer.begin();

  credentialsSaved = false;
  while (true) {
    dnsServer.processNextRequest();
    webServer.handleClient();

    if (credentialsSaved) {
      Serial.println("wifi: saving credentials to NVS");
      Preferences prefs;
      prefs.begin(NVS_NAMESPACE, false); // read-write
      prefs.putString(NVS_KEY_SSID, submittedSsid);
      prefs.putString(NVS_KEY_PASS, submittedPass);
      prefs.end();
      delay(500);
      ESP.restart();
    }
  }
}
```

- [ ] **Step 3: Wire into `water-reminder.ino`**

```cpp
#include "config.h"
#include "wifi_setup.h"
#include <TFT_eSPI.h>
#include <TFT_Touch.h>

TFT_eSPI tft = TFT_eSPI();
TFT_Touch touch = TFT_Touch(TOUCH_DCS, TOUCH_DCLK, TOUCH_DIN, TOUCH_DOUT);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("water-reminder: boot");

  tft.init();
  tft.setRotation(1);
  touch.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting to WiFi...", TFT_HRES / 2, TFT_VRES / 2, 2);

  if (!wifiSetupConnect()) {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Join 'WaterReminder-Setup'", TFT_HRES / 2, TFT_VRES / 2 - 10, 2);
    tft.drawString("WiFi to configure", TFT_HRES / 2, TFT_VRES / 2 + 10, 2);
    wifiSetupStartPortal(); // never returns
  }

  tft.fillScreen(TFT_BLACK);
  tft.drawString("WiFi connected!", TFT_HRES / 2, TFT_VRES / 2, 2);
}

void loop() {
}
```

- [ ] **Step 4: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles with no errors.

- [ ] **Step 5: Flash and verify captive portal**

Run: `arduino-cli upload -p /dev/cu.usbserial-120 --fqbn esp32:esp32:esp32 --board-options UploadSpeed=115200 .`

On-device check (needs the user, since it requires joining a WiFi network from a phone/laptop):
1. Screen should show "Join 'WaterReminder-Setup' WiFi to configure".
2. From a phone/laptop, join the `WaterReminder-Setup` network.
3. Navigate to `http://192.168.4.1/` (or wait for captive-portal auto-redirect) — the setup form should appear.
4. Submit real home WiFi SSID/password.
5. Device should show "Saved. Rebooting..." then reboot and show "WiFi connected!" on the TFT.

- [ ] **Step 6: Verify credential persistence across reboot**

Power-cycle the board (unplug/replug USB) without re-flashing. Expected: it reconnects directly using saved NVS credentials, screen goes straight to "WiFi connected!" without showing the portal prompt.

- [ ] **Step 7: Commit**

```bash
git add wifi_setup.h wifi_setup.cpp water-reminder.ino
git commit -m "Add WiFi captive-portal provisioning

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

---

## Task 3: NTP time sync + idle screen (clock, no schedule logic yet)

**Files:**
- Create: `time_sync.h`
- Create: `time_sync.cpp`
- Create: `ui.h`
- Create: `ui.cpp`
- Modify: `water-reminder.ino`
- Modify: `config.h`

**Interfaces:**
- Consumes: nothing new from other tasks; uses `TFT_eSPI &tft` passed by reference.
- Produces:
  - `time_sync.h`: `bool timeSyncStart();` — calls `configTzTime()` with the Dublin TZ string and blocks briefly waiting for first sync (returns `true` if `time(nullptr)` looks sane — i.e. year > 2020 — within a timeout, `false` otherwise). `bool timeSyncIsValid();` — cheap check of current sync state, used later to show "Time not synced" per spec error handling.
  - `ui.h`: `void uiDrawIdleScreen(TFT_eSPI &tft, const struct tm &nowLocal, const String &statusLine);` — renders time (HH:MM:SS), weekday name, and a caller-supplied status line (used for "Time not synced" now, "Next reminder: HH:MM" / "Outside work hours" in Task 4). Later tasks reuse this signature unchanged.

- [ ] **Step 1: Add TZ string constant to `config.h`**

```c
// Europe/Dublin POSIX TZ string (auto-handles DST: GMT in winter, IST in summer)
#define WATER_REMINDER_TZ "GMT0IST,M3.5.0/1,M10.5.0"
```

- [ ] **Step 2: Write `time_sync.h`**

```c
#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>

// Starts NTP sync using the configured timezone. Blocks up to a few
// seconds waiting for the first successful sync. Returns true if the
// clock now reads a sane date (year > 2020), false if sync hasn't
// landed yet (caller should keep polling timeSyncIsValid()).
bool timeSyncStart();

// Cheap check: does the system clock currently hold a sane, synced time?
bool timeSyncIsValid();

#endif
```

- [ ] **Step 3: Write `time_sync.cpp`**

```cpp
#include "time_sync.h"
#include "config.h"
#include <time.h>

bool timeSyncIsValid() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  return (timeinfo.tm_year + 1900) > 2020;
}

bool timeSyncStart() {
  configTzTime(WATER_REMINDER_TZ, "pool.ntp.org", "time.nist.gov");

  unsigned long start = millis();
  while (!timeSyncIsValid() && millis() - start < 10000) {
    delay(200);
  }

  return timeSyncIsValid();
}
```

- [ ] **Step 4: Write `ui.h`**

```c
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
```

- [ ] **Step 5: Write `ui.cpp`**

```cpp
#include "ui.h"
#include "config.h"

static const char *WEEKDAY_NAMES[7] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

void uiDrawIdleScreen(TFT_eSPI &tft, const struct tm &nowLocal, const String &statusLine) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  char timeBuf[9];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
           nowLocal.tm_hour, nowLocal.tm_min, nowLocal.tm_sec);
  tft.drawString(timeBuf, TFT_HRES / 2, TFT_VRES / 2 - 20, 4);

  const char *weekday = (nowLocal.tm_wday >= 0 && nowLocal.tm_wday < 7)
                          ? WEEKDAY_NAMES[nowLocal.tm_wday] : "?";
  tft.drawString(weekday, TFT_HRES / 2, TFT_VRES / 2 + 20, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(statusLine, TFT_HRES / 2, TFT_VRES / 2 + 45, 2);
}
```

- [ ] **Step 6: Wire into `water-reminder.ino`** — after WiFi connects, start time sync, then loop redrawing the idle screen once per second

```cpp
#include "config.h"
#include "wifi_setup.h"
#include "time_sync.h"
#include "ui.h"
#include <TFT_eSPI.h>
#include <TFT_Touch.h>
#include <time.h>

TFT_eSPI tft = TFT_eSPI();
TFT_Touch touch = TFT_Touch(TOUCH_DCS, TOUCH_DCLK, TOUCH_DIN, TOUCH_DOUT);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("water-reminder: boot");

  tft.init();
  tft.setRotation(1);
  touch.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting to WiFi...", TFT_HRES / 2, TFT_VRES / 2, 2);

  if (!wifiSetupConnect()) {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Join 'WaterReminder-Setup'", TFT_HRES / 2, TFT_VRES / 2 - 10, 2);
    tft.drawString("WiFi to configure", TFT_HRES / 2, TFT_VRES / 2 + 10, 2);
    wifiSetupStartPortal(); // never returns
  }

  tft.fillScreen(TFT_BLACK);
  tft.drawString("Syncing time...", TFT_HRES / 2, TFT_VRES / 2, 2);
  timeSyncStart();
}

void loop() {
  static int lastDrawnSecond = -1;

  time_t now = time(nullptr);
  struct tm nowLocal;
  localtime_r(&now, &nowLocal);

  if (nowLocal.tm_sec != lastDrawnSecond) {
    lastDrawnSecond = nowLocal.tm_sec;
    String status = timeSyncIsValid() ? "" : "Time not synced";
    uiDrawIdleScreen(tft, nowLocal, status);
  }
}
```

- [ ] **Step 7: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles with no errors.

- [ ] **Step 8: Flash and verify on device**

Run: `arduino-cli upload -p /dev/cu.usbserial-120 --fqbn esp32:esp32:esp32 --board-options UploadSpeed=115200 .`

On-device check: screen should show current Dublin local time (HH:MM:SS, ticking once per second) and today's weekday name, matching a phone/computer clock within a second or two. Ask the user to confirm this visually.

- [ ] **Step 9: Commit**

```bash
git add time_sync.h time_sync.cpp ui.h ui.cpp water-reminder.ino config.h
git commit -m "Add NTP time sync and idle clock screen

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

---

## Task 4: Schedule engine (pure logic, debug-fast-mode aware)

**Files:**
- Create: `schedule.h`
- Create: `schedule.cpp`
- Modify: `config.h`

**Interfaces:**
- Consumes: `struct tm` (standard C time struct) as input; no hardware/library deps beyond `<time.h>` — this file is intentionally hardware-free so its logic is easy to read and change in isolation.
- Produces:
  - `struct tm scheduleNextMark(const struct tm &nowLocal);` — returns the next scheduled reminder time (as a `struct tm`, same day) given the current local time. Behavior depends on `DEBUG_FAST_SCHEDULE`: normal mode uses 30-minute marks within Mon–Fri 09:00–18:00; debug mode uses 30-second marks with no day/hour restriction (per spec's debug mode).
  - `bool scheduleIsWithinWindow(const struct tm &nowLocal);` — true if `nowLocal` falls inside the active schedule window (Mon–Fri 09:00–18:00 normally; always true in debug mode).
  - `bool scheduleIsMarkDue(const struct tm &nowLocal, const struct tm &lastFiredMark);` — true if `nowLocal` has reached/passed the next mark after `lastFiredMark` and is still within the window. Task 5 calls this each loop tick and, when true, triggers the alert and updates its own `lastFiredMark` state to the mark that just fired.

- [ ] **Step 1: Write `schedule.h`**

```c
#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <time.h>

// True if nowLocal falls inside the active reminder window.
// Normal mode: Monday-Friday, 09:00-18:00 (exclusive of 18:00 itself).
// Debug mode (DEBUG_FAST_SCHEDULE): always true (no day/hour restriction).
bool scheduleIsWithinWindow(const struct tm &nowLocal);

// Returns the next scheduled reminder mark strictly after lastFiredMark,
// on the same calendar day as nowLocal. Normal mode: 30-minute marks
// (09:00, 09:30, ..., 17:30). Debug mode: 30-second marks.
struct tm scheduleNextMark(const struct tm &nowLocal, const struct tm &lastFiredMark);

// True if nowLocal has reached/passed the next mark after lastFiredMark
// and nowLocal is still within the active window.
bool scheduleIsMarkDue(const struct tm &nowLocal, const struct tm &lastFiredMark);

#endif
```

- [ ] **Step 2: Write `schedule.cpp`**

```cpp
#include "schedule.h"
#include "config.h"

// Compares two struct tm by their time-of-day + date, via mktime's
// underlying representation (safe for same-session comparisons since
// we never cross a TZ change mid-comparison here).
static time_t toEpoch(const struct tm &t) {
  struct tm copy = t;
  return mktime(&copy);
}

bool scheduleIsWithinWindow(const struct tm &nowLocal) {
#ifdef DEBUG_FAST_SCHEDULE
  return true;
#else
  bool isWeekday = (nowLocal.tm_wday >= 1 && nowLocal.tm_wday <= 5); // Mon-Fri
  bool isWorkHour = (nowLocal.tm_hour >= 9 && nowLocal.tm_hour < 18);
  return isWeekday && isWorkHour;
#endif
}

struct tm scheduleNextMark(const struct tm &nowLocal, const struct tm &lastFiredMark) {
#ifdef DEBUG_FAST_SCHEDULE
  const int intervalSeconds = 30;
  time_t lastEpoch = toEpoch(lastFiredMark);
  time_t nextEpoch = lastEpoch + intervalSeconds;
  struct tm result;
  localtime_r(&nextEpoch, &result);
  return result;
#else
  const int intervalMinutes = 30;
  time_t lastEpoch = toEpoch(lastFiredMark);
  time_t nextEpoch = lastEpoch + (intervalMinutes * 60);
  struct tm result;
  localtime_r(&nextEpoch, &result);
  return result;
#endif
}

bool scheduleIsMarkDue(const struct tm &nowLocal, const struct tm &lastFiredMark) {
  if (!scheduleIsWithinWindow(nowLocal)) {
    return false;
  }
  struct tm nextMark = scheduleNextMark(nowLocal, lastFiredMark);
  return toEpoch(nowLocal) >= toEpoch(nextMark);
}
```

- [ ] **Step 3: Compile (schedule.cpp has no dependents wired yet — verify it at least compiles standalone as part of the sketch)**

Add a temporary throwaway line at the end of `setup()` in `water-reminder.ino` to force the compiler to touch the new files:

```cpp
  // Temporary smoke-check for Task 4 compile verification; removed in Task 5.
  struct tm dummyNow = {};
  struct tm dummyLast = {};
  scheduleIsWithinWindow(dummyNow);
```

And add `#include "schedule.h"` near the top of `water-reminder.ino`.

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles with no errors.

- [ ] **Step 4: Remove the temporary smoke-check lines** (Task 5 will wire this in for real)

Delete the 3 lines added in Step 3 from `water-reminder.ino`, but keep the `#include "schedule.h"`.

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles with no errors (include with no use is fine).

- [ ] **Step 5: Commit**

```bash
git add schedule.h schedule.cpp water-reminder.ino
git commit -m "Add pure schedule-engine logic (Mon-Fri 09:00-18:00, 30-min marks)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

---

## Task 5: Alert screen + touch dismiss + full schedule wiring

**Files:**
- Modify: `ui.h`
- Modify: `ui.cpp`
- Modify: `water-reminder.ino`

**Interfaces:**
- Consumes: `scheduleIsMarkDue`, `scheduleNextMark`, `scheduleIsWithinWindow` from Task 4; `TFT_Touch &touch` for dismiss detection.
- Produces: `ui.h` gains `void uiDrawAlertScreen(TFT_eSPI &tft);` — full-screen "Drink Water!" message, no touch handling inside (caller polls `touch.Pressed()` itself, matching the pattern already proven in the Freenove touch example sketches).

- [ ] **Step 1: Add `uiDrawAlertScreen` to `ui.h`**

```c
// Renders the full-screen reminder alert. Caller is responsible for
// polling touch.Pressed() afterward and returning to the idle screen
// (via uiDrawIdleScreen) once the user taps.
void uiDrawAlertScreen(TFT_eSPI &tft);
```

- [ ] **Step 2: Implement in `ui.cpp`**

```cpp
void uiDrawAlertScreen(TFT_eSPI &tft) {
  tft.fillScreen(TFT_BLUE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.drawString("Drink Water!", TFT_HRES / 2, TFT_VRES / 2 - 20, 4);
  tft.drawString("Tap anywhere to dismiss", TFT_HRES / 2, TFT_VRES / 2 + 20, 2);
}
```

- [ ] **Step 3: Wire full state machine into `water-reminder.ino`**

```cpp
#include "config.h"
#include "wifi_setup.h"
#include "time_sync.h"
#include "schedule.h"
#include "ui.h"
#include <TFT_eSPI.h>
#include <TFT_Touch.h>
#include <time.h>

TFT_eSPI tft = TFT_eSPI();
TFT_Touch touch = TFT_Touch(TOUCH_DCS, TOUCH_DCLK, TOUCH_DIN, TOUCH_DOUT);

enum AppState { STATE_IDLE, STATE_ALERT };
static AppState appState = STATE_IDLE;
static struct tm lastFiredMark;
static bool lastFiredMarkInitialized = false;

static String buildStatusLine(const struct tm &nowLocal) {
  if (!timeSyncIsValid()) {
    return "Time not synced";
  }
  if (!scheduleIsWithinWindow(nowLocal)) {
    return "Outside work hours";
  }
  struct tm nextMark = scheduleNextMark(nowLocal, lastFiredMark);
  char buf[24];
  snprintf(buf, sizeof(buf), "Next reminder: %02d:%02d", nextMark.tm_hour, nextMark.tm_min);
  return String(buf);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("water-reminder: boot");

  tft.init();
  tft.setRotation(1);
  touch.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting to WiFi...", TFT_HRES / 2, TFT_VRES / 2, 2);

  if (!wifiSetupConnect()) {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Join 'WaterReminder-Setup'", TFT_HRES / 2, TFT_VRES / 2 - 10, 2);
    tft.drawString("WiFi to configure", TFT_HRES / 2, TFT_VRES / 2 + 10, 2);
    wifiSetupStartPortal(); // never returns
  }

  tft.fillScreen(TFT_BLACK);
  tft.drawString("Syncing time...", TFT_HRES / 2, TFT_VRES / 2, 2);
  timeSyncStart();

  // Initialize lastFiredMark to "now minus a bit" so the device waits for
  // the next mark rather than firing immediately on boot (per spec).
  time_t now = time(nullptr);
  localtime_r(&now, &lastFiredMark);
  lastFiredMarkInitialized = true;
}

void loop() {
  time_t now = time(nullptr);
  struct tm nowLocal;
  localtime_r(&now, &nowLocal);

  if (appState == STATE_IDLE) {
    static int lastDrawnSecond = -1;
    if (nowLocal.tm_sec != lastDrawnSecond) {
      lastDrawnSecond = nowLocal.tm_sec;
      uiDrawIdleScreen(tft, nowLocal, buildStatusLine(nowLocal));
    }

    if (timeSyncIsValid() && lastFiredMarkInitialized &&
        scheduleIsMarkDue(nowLocal, lastFiredMark)) {
      appState = STATE_ALERT;
      uiDrawAlertScreen(tft);
    }
  } else { // STATE_ALERT
    if (touch.Pressed()) {
      delay(50); // debounce, matches pattern from Freenove touch examples
      lastFiredMark = nowLocal; // this mark has now been acknowledged
      appState = STATE_IDLE;
      uiDrawIdleScreen(tft, nowLocal, buildStatusLine(nowLocal));
    }
  }
}
```

- [ ] **Step 4: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles with no errors.

- [ ] **Step 5: Flash with debug fast-schedule enabled, verify full cycle**

Edit `config.h`: uncomment `#define DEBUG_FAST_SCHEDULE`.

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .` then `arduino-cli upload -p /dev/cu.usbserial-120 --fqbn esp32:esp32:esp32 --board-options UploadSpeed=115200 .`

On-device check (ask the user to observe):
1. Idle screen shows time + "Next reminder: HH:MM:SS"-style countdown (debug mode uses 30-sec marks).
2. Within ~30 seconds, screen should switch to the full-screen "Drink Water!" alert.
3. Tapping anywhere on the screen should dismiss the alert and return to the idle screen.
4. This should repeat every ~30 seconds as long as debug mode is on.

- [ ] **Step 6: Disable debug mode for real use**

Edit `config.h`: re-comment `#define DEBUG_FAST_SCHEDULE` (back to disabled).

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
Expected: compiles with no errors.

Run: `arduino-cli upload -p /dev/cu.usbserial-120 --fqbn esp32:esp32:esp32 --board-options UploadSpeed=115200 .`

On-device check: idle screen shows real time; if outside Mon-Fri 09:00-18:00 right now, status line should read "Outside work hours". If inside the window, it should show the correct next half-hour mark and no immediate alert.

- [ ] **Step 7: Commit**

```bash
git add ui.h ui.cpp water-reminder.ino config.h
git commit -m "Wire schedule engine to alert screen with touch dismiss

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

---

## Task 6: README for the project

**Files:**
- Create: `README.md`

**Interfaces:**
- None — documentation only.

- [ ] **Step 1: Write `README.md`**

```markdown
# Water Reminder

An ESP32 + Freenove 2.8" touch TFT (FNK0114B, ST7789) device that shows a
full-screen reminder to drink water every 30 minutes, Monday-Friday,
09:00-18:00 (Europe/Dublin time, DST-aware). Tap the screen to dismiss.

See [docs/superpowers/specs/2026-09-14-water-reminder-design.md](docs/superpowers/specs/2026-09-14-water-reminder-design.md)
for the full design.

## Hardware

- ESP32 dev board + Freenove 2.8" ST7789 touch TFT (FNK0114B)
- USB connection via CH340 serial (macOS: `/dev/cu.usbserial-120`)

## First-time setup

1. Power on the device. If it has no saved WiFi credentials, it starts an
   access point named `WaterReminder-Setup`.
2. From a phone or laptop, join that WiFi network.
3. A setup page should open automatically (or visit `http://192.168.4.1/`).
4. Enter your home WiFi SSID and password, submit.
5. The device reboots, connects to your WiFi, syncs time via NTP, and
   starts running the schedule.

No WiFi credentials are ever stored in this repository.

## Building and flashing

Requires `arduino-cli` with the `esp32:esp32` core (3.3.11) installed, and
the `TFT_eSPI` (configured for FNK0114B) and `TFT_Touch` libraries in the
Arduino sketchbook.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload -p /dev/cu.usbserial-120 --fqbn esp32:esp32:esp32 \
  --board-options UploadSpeed=115200 .
```

## Debug mode

`config.h` has a `DEBUG_FAST_SCHEDULE` toggle that compresses the 30-minute
reminder interval into 30 seconds, with no day/hour restriction, for fast
end-to-end testing. Keep this commented out for normal use.

## Non-goals / not yet built

- No audible beep — no speaker/buzzer hardware attached yet.
- No persistent history of dismissed reminders.
- No remote control or mobile app.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "Add project README

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```
