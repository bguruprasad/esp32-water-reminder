#include "wifi_setup.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>

static const char *NVS_NAMESPACE = "wifi";
static const char *NVS_KEY_SSID = "ssid";
static const char *NVS_KEY_PASS = "pass";
static const char *NVS_KEY_APIKEY = "apikey";
// NVS key names are capped at 15 characters.
static const char *NVS_KEY_EDDISN = "eddisn";
static const char *NVS_KEY_EDDIKEY = "eddikey";
static const char *NVS_KEY_TGTOKEN = "tgtoken";
static const char *NVS_KEY_TGCHAT = "tgchat";
static const char *NVS_KEY_EDSTOP = "edstop";
static const char *NVS_KEY_EDSTART = "edstart";
static const char *NVS_KEY_CHARTMODE = "chartmode";

// Defaults if nothing is stored: stop at noon, start at 22:00.
//
// Noon rather than the Pi controller's 09:00 because that is what was
// asked for; the Pi's time was not a constraint to preserve, just what
// happened to be in its crontab. 22:00 is the Pi's start time and is
// kept, since its notes record 21:00 and 21:30 both failing to take
// effect while 22:00 worked reliably.
static const int EDDI_DEFAULT_STOP_MIN = 12 * 60;
static const int EDDI_DEFAULT_START_MIN = 22 * 60;

// Reads one saved string from NVS. All the credential getters are the
// same three lines around a different key, so they share this rather
// than repeating the open/read/close dance five times.
static String readSetting(const char *key) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true); // read-only
  String value = prefs.getString(key, "");
  prefs.end();
  return value;
}

// Minutes since midnight as "HH:MM", the format an <input type=time>
// both expects and posts back.
static String minutesToHhMm(int mins) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", mins / 60, mins % 60);
  return String(buf);
}

// "HH:MM" back to minutes since midnight, returning fallback on anything
// malformed. A browser without native time input support can post free
// text here, and a half-typed value must not be able to leave the
// schedule in an undefined state.
static int hhMmToMinutes(const String &s, int fallback) {
  if (s.length() < 5 || s.charAt(2) != ':') return fallback;
  int h = s.substring(0, 2).toInt();
  int m = s.substring(3, 5).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return h * 60 + m;
}

// Reads a stored "HH:MM" as minutes, falling back to the default when
// unset or unparseable.
static int readTimeSetting(const char *key, int fallback) {
  return hhMmToMinutes(readSetting(key), fallback);
}

static const char *AP_SSID = "WaterReminder-Setup";
static const byte DNS_PORT = 53;

static DNSServer dnsServer;
static WebServer webServer(80);
static bool credentialsSaved = false;
static String submittedSsid;
static String submittedPass;
static String submittedApiKey;
static String submittedEddiSn;
static String submittedEddiKey;
static String submittedTgToken;
static String submittedTgChat;
static String submittedEdStop;
static String submittedEdStart;
static String submittedChartMode;

// Escapes a stored value for use inside a double-quoted HTML attribute.
// An SSID is user-supplied text that can legitimately contain quotes or
// angle brackets, and dropping one straight into value="..." would end
// the attribute early and mangle (or inject into) the rest of the form.
static String htmlAttr(const String &s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      default: out += c;
    }
  }
  return out;
}

// One text field, prefilled with whatever is stored.
static String fieldPlain(const char *label, const char *name,
                         int maxLen, const String &stored) {
  return String(label) + ":<br><input name='" + name + "' maxlength='" +
         String(maxLen) + "' value=\"" + htmlAttr(stored) + "\"><br>";
}

// One secret field. The stored value is NEVER sent to the browser: the
// portal runs on an open access point, so prefilling a password or an
// API key would broadcast it to anyone in range. The field only reports
// WHETHER something is stored, and staying blank keeps it.
static String fieldSecret(const char *label, const char *name,
                          int maxLen, const String &stored) {
  String hint = stored.length() ? "stored, leave blank to keep"
                                : "not set";
  return String(label) + ":<br><input name='" + name +
         "' type='password' maxlength='" + String(maxLen) +
         "' placeholder='(" + hint + ")'><br>";
}

// Built per request rather than held as a constant, because it now
// reflects what is actually saved on the device.
static String buildFormHtml() {
  String h =
    "<!DOCTYPE html><html><head><meta name='viewport' "
    "content='width=device-width,initial-scale=1'>"
    "<title>Water Reminder Setup</title></head><body>"
    "<h2>Water Reminder: Setup</h2>"
    "<p><small>Blank password and key fields keep whatever is already "
    "saved. Only what you type replaces anything.</small></p>"
    "<form method='POST' action='/save'>";

  h += fieldPlain("SSID", "ssid", 32, readSetting(NVS_KEY_SSID));
  h += fieldSecret("Password", "pass", 64, readSetting(NVS_KEY_PASS));
  h += "<br>";

  h += fieldSecret("Finnhub API key", "apikey", 64,
                   readSetting(NVS_KEY_APIKEY));
  h += "<small>Unset means no price panels.</small><br><br>";

  h += "<hr>Eddi (optional):<br>";
  h += fieldPlain("Hub serial", "eddisn", 32, readSetting(NVS_KEY_EDDISN));
  h += fieldSecret("myenergi API key", "eddikey", 64,
                   readSetting(NVS_KEY_EDDIKEY));
  h += "<small>Unset means no Eddi status pill.</small><br><br>";

  h += fieldSecret("Telegram bot token", "tgtoken", 64,
                   readSetting(NVS_KEY_TGTOKEN));
  h += fieldPlain("Telegram chat ID", "tgchat", 32,
                  readSetting(NVS_KEY_TGCHAT));
  h += "<small>Both needed for Eddi status alerts.</small><br><br>";

  // type=time gives a native picker on a phone and posts "HH:MM".
  h += "<hr>Eddi schedule:<br>";
  h += "Stop at:<br><input name='edstop' type='time' value=\"" +
       minutesToHhMm(readTimeSetting(NVS_KEY_EDSTOP, EDDI_DEFAULT_STOP_MIN)) +
       "\"><br>";
  h += "Start at:<br><input name='edstart' type='time' value=\"" +
       minutesToHhMm(readTimeSetting(NVS_KEY_EDSTART, EDDI_DEFAULT_START_MIN)) +
       "\"><br>";
  h += "<small>Local time, every day. A scheduled time that passes while "
       "the device is off is skipped, not caught up.</small><br><br>";

  // Stored as a word rather than a number, so a stale or hand-edited
  // value reads as the default instead of silently selecting something.
  {
    String cm = readSetting(NVS_KEY_CHARTMODE);
    h += "<hr>Price chart:<br><select name='chartmode'>";
    h += "<option value='candle'";
    if (cm != "alt" && cm != "line") h += " selected";
    h += ">Candles only (last 30 min)</option>";
    h += "<option value='alt'";
    if (cm == "alt") h += " selected";
    h += ">Alternate line and candles</option>";
    h += "<option value='line'";
    if (cm == "line") h += " selected";
    h += ">Line only (whole day)</option>";
    h += "</select><br>";
    h += "<small>One view halves the data fetched, so it refreshes every "
         "2 minutes instead of 4.</small><br><br>";
  }

  h += "<input type='submit' value='Save and Connect'>"
       "</form></body></html>";
  return h;
}

static void handleRoot() {
  webServer.send(200, "text/html", buildFormHtml());
}

// A blank secret field means "keep what is stored", not "erase it".
// The browser is never sent the stored secret, so a blank submission is
// the only thing a user who did not want to change it can send back.
// Treating that as an erase is how a single visit to fix an SSID used
// to wipe every key on the device.
static String keepIfBlank(const String &submitted, const char *nvsKey) {
  return submitted.length() ? submitted : readSetting(nvsKey);
}

static void handleSave() {
  // Plain fields are prefilled, so what comes back is normally the full
  // intended value. Blank still means keep: a browser that fails to
  // render a field, or a form submitted from a stale cached page, would
  // otherwise wipe the hub serial and take the pill and the entire
  // schedule down with it, silently and with nothing on screen to say
  // why. Clearing one of these deliberately is not a thing anyone needs
  // to do from this page.
  submittedSsid = keepIfBlank(webServer.arg("ssid"), NVS_KEY_SSID);
  submittedEddiSn = keepIfBlank(webServer.arg("eddisn"), NVS_KEY_EDDISN);
  submittedTgChat = keepIfBlank(webServer.arg("tgchat"), NVS_KEY_TGCHAT);

  // Normalised through the parser before storage, so what lands in NVS
  // is always a valid "HH:MM" and readers never have to defend against
  // whatever the browser happened to post.
  submittedEdStop = minutesToHhMm(
    hhMmToMinutes(webServer.arg("edstop"),
                  readTimeSetting(NVS_KEY_EDSTOP, EDDI_DEFAULT_STOP_MIN)));
  submittedEdStart = minutesToHhMm(
    hhMmToMinutes(webServer.arg("edstart"),
                  readTimeSetting(NVS_KEY_EDSTART, EDDI_DEFAULT_START_MIN)));

  // Only the three known words are accepted; anything else stores the
  // default rather than a value no reader understands.
  {
    String cm = webServer.arg("chartmode");
    submittedChartMode = (cm == "alt" || cm == "line") ? cm : String("candle");
  }

  // Secret fields come back blank unless deliberately retyped.
  submittedPass = keepIfBlank(webServer.arg("pass"), NVS_KEY_PASS);
  submittedApiKey = keepIfBlank(webServer.arg("apikey"), NVS_KEY_APIKEY);
  submittedEddiKey = keepIfBlank(webServer.arg("eddikey"), NVS_KEY_EDDIKEY);
  submittedTgToken = keepIfBlank(webServer.arg("tgtoken"), NVS_KEY_TGTOKEN);

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
      prefs.putString(NVS_KEY_APIKEY, submittedApiKey);
      prefs.putString(NVS_KEY_EDDISN, submittedEddiSn);
      prefs.putString(NVS_KEY_EDDIKEY, submittedEddiKey);
      prefs.putString(NVS_KEY_TGTOKEN, submittedTgToken);
      prefs.putString(NVS_KEY_TGCHAT, submittedTgChat);
      prefs.putString(NVS_KEY_EDSTOP, submittedEdStop);
      prefs.putString(NVS_KEY_EDSTART, submittedEdStart);
      prefs.putString(NVS_KEY_CHARTMODE, submittedChartMode);
      prefs.end();
      delay(500);
      ESP.restart();
    }
  }
}

String wifiSetupApiKey() {
  return readSetting(NVS_KEY_APIKEY);
}

String wifiSetupEddiSerial() {
  return readSetting(NVS_KEY_EDDISN);
}

String wifiSetupEddiKey() {
  return readSetting(NVS_KEY_EDDIKEY);
}

String wifiSetupTgToken() {
  return readSetting(NVS_KEY_TGTOKEN);
}

String wifiSetupTgChat() {
  return readSetting(NVS_KEY_TGCHAT);
}

int wifiSetupEddiStopMin() {
  return readTimeSetting(NVS_KEY_EDSTOP, EDDI_DEFAULT_STOP_MIN);
}

int wifiSetupEddiStartMin() {
  return readTimeSetting(NVS_KEY_EDSTART, EDDI_DEFAULT_START_MIN);
}

int wifiSetupChartMode() {
  String cm = readSetting(NVS_KEY_CHARTMODE);
  if (cm == "alt") return 0;
  if (cm == "line") return 2;
  return 1; // candles, the default and the fallback for anything unknown
}
