#include "wifi_setup.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>

static const char *NVS_NAMESPACE = "wifi";
static const char *NVS_KEY_SSID = "ssid";
static const char *NVS_KEY_PASS = "pass";
static const char *NVS_KEY_APIKEY = "apikey";
static const char *AP_SSID = "WaterReminder-Setup";
static const byte DNS_PORT = 53;

static DNSServer dnsServer;
static WebServer webServer(80);
static bool credentialsSaved = false;
static String submittedSsid;
static String submittedPass;
static String submittedApiKey;

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

static void handleRoot() {
  webServer.send(200, "text/html", FORM_HTML);
}

static void handleSave() {
  submittedSsid = webServer.arg("ssid");
  submittedPass = webServer.arg("pass");
  submittedApiKey = webServer.arg("apikey");
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
      prefs.end();
      delay(500);
      ESP.restart();
    }
  }
}

String wifiSetupApiKey() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true); // read-only
  String key = prefs.getString(NVS_KEY_APIKEY, "");
  prefs.end();
  return key;
}
