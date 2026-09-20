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
  // Plain fields are prefilled in the form, so what comes back is
  // already the full intended value and replaces the stored one.
  submittedSsid = webServer.arg("ssid");
  submittedEddiSn = webServer.arg("eddisn");
  submittedTgChat = webServer.arg("tgchat");

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
