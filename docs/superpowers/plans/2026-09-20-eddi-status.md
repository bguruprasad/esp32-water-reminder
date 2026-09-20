# Eddi Status Pill and Telegram Alerts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the myenergi Eddi's live status as a colour-coded pill on the idle screen, with the weekday beside it in the same colour, and send a Telegram message whenever the observed status actually changes.

**Architecture:** Two new modules shaped like the existing `chart.cpp`: `eddi.cpp` polls the myenergi cloud over hand-rolled HTTP Digest auth and owns the status state; `notify.cpp` posts to the Telegram Bot API. The idle screen gains one row between the clock and the first chart panel. Every failure path is a no-op, so an outage costs the pill only and never the clock, the reminder, or the charts.

**Tech Stack:** ESP32 Arduino core 3.3.11, TFT_eSPI, ArduinoJson 7.4.3, `MD5Builder.h` (core), `HTTPClient` / `NetworkClientSecure` / `Preferences` (core). No new third-party libraries.

**Spec:** [docs/superpowers/specs/2026-09-20-eddi-status-design.md](../specs/2026-09-20-eddi-status-design.md)

## Global Constraints

- Board: FQBN `esp32:esp32:esp32`, port `/dev/cu.usbserial-1120` (the trailing number changes between replugs; check `ls /dev/cu.usb*`), upload speed `115200`.
- **Partition scheme `no_fs` must be passed on every compile and upload.** A default-scheme flash writes a different partition table and erases the saved credentials. Build: `arduino-cli compile --fqbn esp32:esp32:esp32 --board-options PartitionScheme=no_fs .` Upload: `arduino-cli upload -p /dev/cu.usbserial-1120 --fqbn esp32:esp32:esp32 --board-options PartitionScheme=no_fs,UploadSpeed=115200 .`
- **No em dashes and no non-ASCII characters** in any tracked file or commit message. Verify with Python, not `grep -P`, which has given a false clean on this repo before.
- **No `Co-Authored-By` trailers** in commit messages.
- **No secret is ever written to a file, a commit, or the conversation.** The myenergi hub serial, myenergi API key, Telegram bot token and Telegram chat ID are typed into the device's setup portal by the owner only.
- Poll myenergi at most once per 30 seconds. Community guidance is one request per 10 seconds, with a stated risk of a temporary IP ban.
- NVS key names must be 15 characters or fewer.
- Partial-redraw discipline: never call `fillScreen` outside a full repaint, and repaint a region only when its content has changed. This is what removed the screen flicker twice.
- `config.h` pin defines are deliberately column-aligned. Do not reformat them.

---

### Task 1: Digest auth spike (throwaway)

This answers the one genuine unknown before anything is built on it: whether a hand-rolled HTTP Digest handshake succeeds against the myenergi director. The output is an answer, not code that ships. If it fails, the design changes fundamentally and Tasks 2-5 are void.

**Files:**
- Create: `/private/tmp/claude-501/-Users-guru-Work-ESP32Projects/18b2dcc2-c652-44e0-87ba-fcbccecca37e/scratchpad/eddi_spike/eddi_spike.ino` (scratchpad, NOT the repo)

**Interfaces:**
- Consumes: nothing.
- Produces: a yes/no answer recorded in the ledger. No code that later tasks depend on.

- [ ] **Step 1: Write the spike sketch**

The owner enters WiFi and myenergi credentials directly into this sketch's constants on their own machine, runs it, and clears them afterwards. Ask them to fill the four blanks; never fill them yourself and never echo what they enter.

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <MD5Builder.h>
#include <TFT_eSPI.h>

// Filled in by the owner locally. Never committed, never echoed.
const char *WIFI_SSID = "";
const char *WIFI_PASS = "";
const char *HUB_SERIAL = "";  // Digest username
const char *API_KEY    = "";  // Digest password

TFT_eSPI tft = TFT_eSPI();

static String md5Hex(const String &s) {
  MD5Builder b;
  b.begin();
  b.add(s);
  b.calculate();
  return b.toString();
}

// Pulls one quoted value out of a WWW-Authenticate header.
static String headerField(const String &hdr, const String &key) {
  int i = hdr.indexOf(key + "=\"");
  if (i < 0) return "";
  i += key.length() + 2;
  int j = hdr.indexOf('"', i);
  if (j < 0) return "";
  return hdr.substring(i, j);
}

static void line(int row, const String &s) {
  tft.fillRect(0, row * 20, 240, 20, TFT_BLACK);
  tft.drawString(s, 4, row * 20, 2);
}

void setup() {
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  line(0, "WiFi ok");

  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);

  const char *uri = "/cgi-jstatus-*";
  String url = String("https://director.myenergi.net") + uri;

  // Hop 1: expect 401 carrying the Digest challenge.
  const char *collect[] = {"WWW-Authenticate", "X_MYENERGI-asn"};
  http.begin(client, url);
  http.collectHeaders(collect, 2);
  int code = http.GET();
  line(1, "hop1 code=" + String(code));

  String chal = http.header("WWW-Authenticate");
  http.end();
  if (code != 401 || chal.length() == 0) {
    line(2, "no challenge, STOP");
    return;
  }

  String realm = headerField(chal, "realm");
  String nonce = headerField(chal, "nonce");
  String qop   = headerField(chal, "qop");
  line(2, "realm=" + realm.substring(0, 18));

  String cnonce = "a1b2c3d4";
  String nc = "00000001";
  String ha1 = md5Hex(String(HUB_SERIAL) + ":" + realm + ":" + String(API_KEY));
  String ha2 = md5Hex(String("GET:") + uri);
  String resp = md5Hex(ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2);

  String auth = "Digest username=\"" + String(HUB_SERIAL) + "\", realm=\"" + realm +
                "\", nonce=\"" + nonce + "\", uri=\"" + uri +
                "\", qop=" + qop + ", nc=" + nc + ", cnonce=\"" + cnonce +
                "\", response=\"" + resp + "\"";

  // Hop 2: same URL, now authenticated.
  http.begin(client, url);
  http.collectHeaders(collect, 2);
  http.addHeader("Authorization", auth);
  code = http.GET();
  line(3, "hop2 code=" + String(code));

  if (code == 200) {
    String asn = http.header("X_MYENERGI-asn");
    line(4, "asn=" + asn);
    String body = http.getString();
    line(5, "len=" + String(body.length()));
    int e = body.indexOf("\"eddi\"");
    line(6, e >= 0 ? "eddi found" : "no eddi key");
    if (e >= 0) line(7, body.substring(e, e + 60));
  }
  http.end();
}

void loop() {}
```

- [ ] **Step 2: Ask the owner to fill in the four constants and run it**

Give them the exact build and upload commands from Global Constraints, with the sketch directory substituted. Do not ask them to paste any value back.

- [ ] **Step 3: Read the result off the screen**

Expected on success: `hop1 code=401`, `hop2 code=200`, an `asn=` line naming a server, and `eddi found`.

Record in the ledger which of these happened:
- `hop2 code=200` and `eddi found`: Digest works. Proceed to Task 2.
- `hop2 code=401`: the handshake is wrong. Inspect the challenge; myenergi may require `algorithm=MD5-sess` or a different `qop`. One retry with the corrected computation, then escalate.
- `hop1` not 401: the director did not challenge. The endpoint or TLS path is wrong, not the auth.

- [ ] **Step 4: Have the owner clear the credentials from the sketch**

Then delete the scratchpad directory. Nothing from this task is committed.

- [ ] **Step 5: Record the ruling**

If Digest failed and one corrected retry also failed, STOP and report. The fallback is the Raspberry Pi pushing state to the device over plain HTTP on the local network, which sidesteps myenergi authentication entirely and is a different spec. Do not attempt it under this plan.

---

### Task 2: Portal fields and NVS storage

**Files:**
- Modify: `wifi_setup.cpp` (form HTML, save handler, new getters)
- Modify: `wifi_setup.h` (declare the getters)

**Interfaces:**
- Consumes: the existing `NVS_NAMESPACE` "wifi" and the `wifiSetupApiKey()` pattern.
- Produces: `String wifiSetupEddiSerial()`, `String wifiSetupEddiKey()`, `String wifiSetupTgToken()`, `String wifiSetupTgChat()`. All return `""` when unset.

- [ ] **Step 1: Add the NVS key constants**

In `wifi_setup.cpp`, beside the existing key constants. All are within the 15-character NVS limit.

```cpp
static const char *NVS_KEY_EDDISN = "eddisn";
static const char *NVS_KEY_EDDIKEY = "eddikey";
static const char *NVS_KEY_TGTOKEN = "tgtoken";
static const char *NVS_KEY_TGCHAT = "tgchat";
```

- [ ] **Step 2: Add the four fields to the form**

Extend `FORM_HTML`, inserting before the submit input. Keep the existing string-literal concatenation style.

```cpp
  "<hr>Eddi (optional):<br>"
  "Hub serial:<br><input name='eddisn' maxlength='32'><br>"
  "myenergi API key:<br>"
  "<input name='eddikey' type='password' maxlength='64'><br>"
  "<small>Leave blank to run without the Eddi pill.</small><br><br>"
  "Telegram bot token (optional):<br>"
  "<input name='tgtoken' type='password' maxlength='64'><br>"
  "Telegram chat ID:<br><input name='tgchat' maxlength='32'><br>"
  "<small>Both needed for status change alerts.</small><br><br>"
```

- [ ] **Step 3: Capture and persist them**

Add four statics beside `submittedApiKey`, read them in `handleSave()` exactly as `apikey` is read, and write all four in the `prefs.putString` block in `wifiSetupStartPortal()`.

- [ ] **Step 4: Add the four getters**

Follow `wifiSetupApiKey()` exactly: open the namespace read-only, read the key with a `""` default, close, return.

- [ ] **Step 5: Compile**

Run the Global Constraints compile command. Expected: success, size still near 56%.

- [ ] **Step 6: Flash and verify persistence**

Flashing does NOT erase NVS as long as `PartitionScheme=no_fs` is passed, so the existing WiFi and Finnhub credentials survive. The new fields start empty.

Ask the owner to reach the portal and enter the four values. To force the portal without erasing anything, they can temporarily move the device out of WiFi range, or enter a wrong SSID. Verify after reboot that WiFi still connects and prices still appear, which proves the existing keys were not disturbed.

- [ ] **Step 7: Commit**

```bash
git add wifi_setup.cpp wifi_setup.h
git commit -m "Add Eddi and Telegram fields to the setup portal"
```

---

### Task 3: The notify module

Built before the Eddi module, which is its only caller: `eddi.cpp`
includes `notify.h` and calls `notifySend()`, so it cannot compile until
this exists. Nothing here depends on the Eddi module in return.

**Files:**
- Create: `notify.cpp`, `notify.h`

**Interfaces:**
- Consumes: `wifiSetupTgToken()`, `wifiSetupTgChat()` from Task 2.
- Produces: `bool notifySend(const String &text);` returns true on a confirmed send. Callers may ignore the result.

- [ ] **Step 1: Write `notify.h`**

```cpp
#ifndef NOTIFY_H
#define NOTIFY_H

#include <Arduino.h>

// Call once from setup(), after WiFi is up.
void notifyBegin();

// Posts one message to Telegram. Returns false and does nothing when
// no token or chat ID is configured. A failed send is dropped, never
// queued or retried: a heater status is not worth a retry queue, and a
// queue that retries every tick is a way to get a bot rate limited.
bool notifySend(const String &text);

#endif
```

- [ ] **Step 2: Write `notify.cpp`**

```cpp
#include "notify.h"
#include "wifi_setup.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

static String token_, chat_;

void notifyBegin() {
  token_ = wifiSetupTgToken();
  chat_ = wifiSetupTgChat();
}

// Percent-encodes the characters that would break a form body.
static String urlEncode(const String &s) {
  String out;
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

bool notifySend(const String &text) {
  if (token_.length() == 0 || chat_.length() == 0) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  String url = "https://api.telegram.org/bot" + token_ + "/sendMessage";
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String payload = "chat_id=" + urlEncode(chat_) + "&text=" + urlEncode(text);
  int code = http.POST(payload);
  http.end();
  return code == 200;
}
```

- [ ] **Step 3: Call `notifyBegin()` in setup**

Beside `eddiBegin();` in `water-reminder.ino`.

- [ ] **Step 4: Compile and flash**

- [ ] **Step 5: Verify a real send**

The Eddi will not change state on demand, so prove the Telegram path independently: temporarily add `notifySend("water-reminder: telegram test");` at the end of `setup()`, flash, confirm the message arrives, then remove it and flash again. Do not commit the temporary call.

- [ ] **Step 6: Commit**

```bash
git add notify.cpp notify.h water-reminder.ino
git commit -m "Add the Telegram notify module"
```

---

### Task 4: The eddi module

**Files:**
- Create: `eddi.cpp`, `eddi.h`
- Modify: `water-reminder.ino` (include, `eddiBegin()` in setup, `eddiPump()` in loop)

**Interfaces:**
- Consumes: `wifiSetupEddiSerial()`, `wifiSetupEddiKey()` from Task 2, and
  `notifySend()` from Task 3.
- Produces:
  - `void eddiBegin();`
  - `void eddiPump();`
  - `int eddiStatus();` returns the raw `sta` code, or `EDDI_UNKNOWN` (-1).
  - `const char *eddiStatusText(int sta);` returns the short display word.
  - `uint16_t eddiStatusColor(int sta);` returns the TFT colour.
  - `bool eddiChanged();` true once after the displayed status changes; clears on read, so the UI knows when to repaint.

- [ ] **Step 1: Write `eddi.h`**

```cpp
#ifndef EDDI_H
#define EDDI_H

#include <Arduino.h>

// Sentinel for "never fetched" or "unreachable".
static const int EDDI_UNKNOWN = -1;

// Call once from setup(), after WiFi is up.
void eddiBegin();

// Call every loop(). Non-blocking apart from the fetch itself, which
// runs at most once per 30s and is capped by a 5s timeout.
void eddiPump();

// Last observed sta code, or EDDI_UNKNOWN.
int eddiStatus();

// Short display word for a sta code. Deliberately short: the full
// "Max Temp Reached" overflows the 240px row beside a weekday.
const char *eddiStatusText(int sta);

// Display colour for a sta code.
uint16_t eddiStatusColor(int sta);

// True once after the status changes, cleared on read, so the UI
// repaints only when there is something new.
bool eddiChanged();

#endif
```

- [ ] **Step 2: Write `eddi.cpp` status mapping and state**

Status codes are from the owner's own Python controller: 1 Paused, 3 Diverting, 4 Boosting, 5 Max Temp Reached, 6 Stopped.

```cpp
#include "eddi.h"
#include "wifi_setup.h"
#include "notify.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <MD5Builder.h>
#include <ArduinoJson.h>
#include <time.h>

static const unsigned long EDDI_POLL_INTERVAL_MS = 30000;
static const char *DIRECTOR_HOST = "director.myenergi.net";
// Known server for this hub, used when director discovery fails so a
// director outage does not disable the pill.
static const char *FALLBACK_HOST = "s18.myenergi.net";

static String serial_, key_, host_;
static int status_ = EDDI_UNKNOWN;
static int lastNotified_ = EDDI_UNKNOWN;
static bool firstFetchDone_ = false;
static bool changed_ = false;
static unsigned long lastPollAtMs = 0;

const char *eddiStatusText(int sta) {
  switch (sta) {
    case 1: return "Paused";
    case 3: return "Diverting";
    case 4: return "Boosting";
    case 5: return "Max Temp";
    case 6: return "Stopped";
    default: return "Eddi --";
  }
}

uint16_t eddiStatusColor(int sta) {
  switch (sta) {
    case 3: case 4: return TFT_GREEN;   // heating now
    case 1: case 5: return TFT_ORANGE;  // on, not heating
    case 6: return TFT_LIGHTGREY;       // stopped
    default: return TFT_DARKGREY;       // unknown
  }
}

int eddiStatus() { return status_; }

bool eddiChanged() {
  bool c = changed_;
  changed_ = false;
  return c;
}

void eddiBegin() {
  serial_ = wifiSetupEddiSerial();
  key_ = wifiSetupEddiKey();
  host_ = "";
  lastPollAtMs = millis() - EDDI_POLL_INTERVAL_MS; // allow an immediate first poll
}
```

- [ ] **Step 3: Add the Digest helpers**

Reuse the exact computation proven in Task 1.

```cpp
static String md5Hex(const String &s) {
  MD5Builder b;
  b.begin();
  b.add(s);
  b.calculate();
  return b.toString();
}

static String headerField(const String &hdr, const String &key) {
  int i = hdr.indexOf(key + "=\"");
  if (i < 0) return "";
  i += key.length() + 2;
  int j = hdr.indexOf('"', i);
  if (j < 0) return "";
  return hdr.substring(i, j);
}

static String buildAuth(const String &chal, const String &uri) {
  String realm = headerField(chal, "realm");
  String nonce = headerField(chal, "nonce");
  String qop = headerField(chal, "qop");
  if (realm.length() == 0 || nonce.length() == 0) return "";

  String cnonce = String(millis(), HEX);
  String nc = "00000001";
  String ha1 = md5Hex(serial_ + ":" + realm + ":" + key_);
  String ha2 = md5Hex("GET:" + uri);
  String resp = md5Hex(ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2);

  return "Digest username=\"" + serial_ + "\", realm=\"" + realm +
         "\", nonce=\"" + nonce + "\", uri=\"" + uri +
         "\", qop=" + qop + ", nc=" + nc + ", cnonce=\"" + cnonce +
         "\", response=\"" + resp + "\"";
}
```

- [ ] **Step 4: Add the authenticated GET**

Returns the body on success and an empty string on any failure. Captures the `X_MYENERGI-asn` header so the caller can learn the real host.

```cpp
// Performs the 401-then-retry Digest exchange. Returns "" on any
// failure; every failure here is a no-op by design.
static String digestGet(const String &host, const String &uri, String *asnOut) {
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  String url = "https://" + host + uri;
  const char *collect[] = {"WWW-Authenticate", "X_MYENERGI-asn"};

  if (!http.begin(client, url)) return "";
  http.collectHeaders(collect, 2);
  int code = http.GET();
  if (code != 401) { http.end(); return ""; }
  String chal = http.header("WWW-Authenticate");
  http.end();

  String auth = buildAuth(chal, uri);
  if (auth.length() == 0) return "";

  if (!http.begin(client, url)) return "";
  http.collectHeaders(collect, 2);
  http.addHeader("Authorization", auth);
  code = http.GET();
  String body;
  if (code == 200) {
    if (asnOut) *asnOut = http.header("X_MYENERGI-asn");
    body = http.getString();
  }
  http.end();
  return body;
}
```

- [ ] **Step 5: Add the pump with transition detection**

Three cases deliberately do not notify: the first successful fetch after boot, and any transition into or out of `EDDI_UNKNOWN`. Otherwise a power cut or a WiFi blip would produce false alerts.

```cpp
void eddiPump() {
  if (serial_.length() == 0 || key_.length() == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastPollAtMs < EDDI_POLL_INTERVAL_MS) return;
  lastPollAtMs = millis();

  // Discover the hub's server once, falling back to the known one.
  if (host_.length() == 0) {
    String asn;
    digestGet(DIRECTOR_HOST, "/cgi-jstatus-*", &asn);
    asn.trim();
    if (asn.length() == 0) host_ = FALLBACK_HOST;
    else if (asn.indexOf(".myenergi.net") >= 0) host_ = asn;
    else host_ = asn + ".myenergi.net";
  }

  String body = digestGet(host_, "/cgi-jstatus-E", nullptr);
  if (body.length() == 0) return; // keep the last known status on screen

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  JsonArray arr = doc["eddi"];
  if (arr.isNull() || arr.size() == 0) return;
  int sta = arr[0]["sta"] | EDDI_UNKNOWN;
  if (sta == EDDI_UNKNOWN) return;

  if (sta != status_) {
    status_ = sta;
    changed_ = true;
  }

  // Notify only on a real observed transition between two known states.
  bool suppress = !firstFetchDone_ || lastNotified_ == EDDI_UNKNOWN;
  firstFetchDone_ = true;
  if (!suppress && sta != lastNotified_) {
    struct tm lt;
    time_t now = time(nullptr);
    localtime_r(&now, &lt);
    char buf[96];
    snprintf(buf, sizeof(buf), "Eddi: %s -> %s at %02d:%02d",
             eddiStatusText(lastNotified_), eddiStatusText(sta),
             lt.tm_hour, lt.tm_min);
    notifySend(String(buf));
  }
  lastNotified_ = sta;
}
```

- [ ] **Step 6: Wire into the sketch**

Add `#include "eddi.h"`, call `eddiBegin();` beside `chartBegin();` in `setup()`, and `eddiPump();` beside `chartPump();` in the `STATE_IDLE` branch of `loop()`.

- [ ] **Step 7: Compile, flash, verify**

Expected on screen: nothing yet, since the UI comes in Task 5. Verify instead that the clock, prices and charts still behave exactly as before, which proves the new polling did not disturb the loop.

- [ ] **Step 8: Commit**

```bash
git add eddi.cpp eddi.h water-reminder.ino
git commit -m "Add the Eddi module: Digest auth, polling, transition detection"
```

---

### Task 5: The status row

**Files:**
- Modify: `ui.cpp` (new row renderer, `IDLE_CLOCK_Y` change, calls from both idle entry points)
- Modify: `ui.h` (document the new row)
- Modify: `water-reminder.ino` (repaint when `eddiChanged()`)

**Interfaces:**
- Consumes: `eddiStatus()`, `eddiStatusText()`, `eddiStatusColor()`, `eddiChanged()` from Task 4.
- Produces: no new public UI function; the row is painted inside `uiDrawIdleScreen` and `uiUpdateIdleScreen`.

- [ ] **Step 1: Raise the clock and add the row constants**

The gap between the clock block and the first panel is currently y=64 to y=84, which is 20px: too small for a 26px pill. Moving `IDLE_CLOCK_Y` from 34 to 28 raises the clock block by 6px and opens the gap to 26px. The panels do not move, because `PANEL_TOP[1] + PANEL_H` is already exactly 320.

In `ui.cpp`, change `IDLE_CLOCK_Y` to 28 and add beside it:

```cpp
// Status row: Eddi pill on the left, weekday on the right, both in the
// Eddi's status colour so the row reads as one indicator.
//
// Both labels are abbreviated because the pair overflows 240px
// otherwise: "Max Temp Reached" alone is about 160px before padding,
// and a full weekday adds about 90px.
static const int IDLE_ROW_Y = 72;   // vertical centre of the row
static const int IDLE_ROW_H = 24;
static const char *WEEKDAY_ABBR[7] = {"Sun", "Mon", "Tue", "Wed",
                                      "Thu", "Fri", "Sat"};
```

- [ ] **Step 2: Add the row renderer**

```cpp
// Repaints the status row. Clears its full width first, since both the
// pill width and the weekday change independently.
static void paintStatusRow(TFT_eSPI &tft, const struct tm &nowLocal) {
  tft.fillRect(0, IDLE_ROW_Y - IDLE_ROW_H / 2, TFT_HRES, IDLE_ROW_H, TFT_BLACK);

  int sta = eddiStatus();
  uint16_t col = eddiStatusColor(sta);
  const char *label = eddiStatusText(sta);

  tft.setFreeFont(&FreeSansBold9pt7b);

  // Pill, left aligned.
  tft.setTextDatum(ML_DATUM);
  int textW = tft.textWidth(label);
  int pillW = textW + 20;
  tft.drawRoundRect(4, IDLE_ROW_Y - IDLE_ROW_H / 2, pillW, IDLE_ROW_H,
                    IDLE_ROW_H / 2, col);
  tft.setTextColor(col, TFT_BLACK);
  tft.drawString(label, 14, IDLE_ROW_Y);

  // Weekday, right aligned, same colour.
  tft.setTextDatum(MR_DATUM);
  tft.drawString(WEEKDAY_ABBR[nowLocal.tm_wday % 7], TFT_HRES - 6, IDLE_ROW_Y);

  tft.setFreeFont(NULL);
  tft.setTextDatum(MC_DATUM);
}
```

- [ ] **Step 3: Track what was painted and call it**

Add `static int lastRowSta = EDDI_UNKNOWN - 1;` and `static int lastRowWday = -1;` beside the other paint-tracking statics.

In `uiDrawIdleScreen`, call `paintStatusRow(tft, nowLocal);` after `paintClockRow(...)`, and record both values. Delete the `(void)statusLine;` line only if nothing else uses it; the parameter stays for call-site compatibility.

In `uiUpdateIdleScreen`, repaint the row only when the status or the weekday has changed:

```cpp
  int sta = eddiStatus();
  if (sta != lastRowSta || nowLocal.tm_wday != lastRowWday) {
    paintStatusRow(tft, nowLocal);
    lastRowSta = sta;
    lastRowWday = nowLocal.tm_wday;
  }
```

- [ ] **Step 4: Add the include**

`#include "eddi.h"` at the top of `ui.cpp`.

- [ ] **Step 5: Compile**

- [ ] **Step 6: Flash and verify on hardware**

Check each in turn, and expect at least one constant to need nudging, exactly as the portrait constants did:
- The row sits cleanly between the clock and the first panel, touching neither.
- The pill does not collide with the weekday at the longest status.
- The clock is not clipped at the top after the 6px rise.
- Both chart panels still render fully, with nothing lost off the bottom.

- [ ] **Step 7: Commit**

```bash
git add ui.cpp ui.h water-reminder.ino
git commit -m "Add the Eddi status row to the idle screen"
```

---

### Task 6: Documentation and close-out

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Document the feature**

Add a section covering what the pill shows, the colour mapping, the four new portal fields, that the myenergi API is unofficial and may change, and that the pill reflects cloud state which can lag the physical device by 3-5 minutes.

- [ ] **Step 2: Run the non-ASCII scan**

```bash
git ls-files -z | python3 -c "
import sys,pathlib
bad=0
for name in sys.stdin.buffer.read().split(b'\0'):
    if not name: continue
    p=pathlib.Path(name.decode())
    if not p.is_file(): continue
    try: t=p.read_text(encoding='utf-8')
    except Exception: continue
    for i,line in enumerate(t.splitlines(),1):
        for ch in line:
            if ord(ch)>127:
                print(f'{p}:{i}: U+{ord(ch):04X}'); bad+=1
print('non-ASCII in tracked files:', bad)
"
```

Expected: 0.

- [ ] **Step 3: Leave it running across a real transition**

The Pi's cron stops the Eddi at 09:00 and starts it at 22:00. Leave the device running across one of those and confirm exactly one Telegram message arrives, naming the correct transition, and that the pill colour changes to match.

- [ ] **Step 4: Commit and finish the branch**

```bash
git add README.md
git commit -m "Document the Eddi status pill"
```

Then use superpowers:finishing-a-development-branch.
