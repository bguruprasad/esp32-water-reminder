#include "eddi.h"
#include "wifi_setup.h"
#include "notify.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <MD5Builder.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>

// 30s, against community guidance of one request per 10s for this API.
// Exceeding that risks a temporary IP ban, and the status of a water
// heater does not change fast enough to want a tighter loop.
static const unsigned long EDDI_POLL_INTERVAL_MS = 30000;

static const char *DIRECTOR_HOST = "director.myenergi.net";
// Known server for this hub, used when director discovery fails so a
// director outage costs the pill nothing.
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
    case 5: return "Max Temp"; // "Max Temp Reached" overflows the row
    case 6: return "Stopped";
    default: return "Eddi --";
  }
}

uint16_t eddiStatusColor(int sta) {
  switch (sta) {
    case 3: case 4: return TFT_GREEN;     // heating now
    case 1: case 5: return TFT_ORANGE;    // on, not heating
    case 6: return TFT_LIGHTGREY;         // stopped
    default: return TFT_DARKGREY;         // unknown or unreachable
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
  lastPollAtMs = millis() - EDDI_POLL_INTERVAL_MS; // allow an immediate poll
}

static String md5Hex(const String &s) {
  MD5Builder b;
  b.begin();
  b.add(s);
  b.calculate();
  return b.toString();
}

// Pulls one quoted value out of a WWW-Authenticate header.
static String headerField(const String &hdr, const String &name) {
  int i = hdr.indexOf(name + "=\"");
  if (i < 0) return "";
  i += name.length() + 2;
  int j = hdr.indexOf('"', i);
  if (j < 0) return "";
  return hdr.substring(i, j);
}

// Builds the Authorization header for a Digest challenge. This exact
// computation was verified against the real director during the spike:
// hop 1 returned 401, hop 2 with this header returned 200.
static String buildAuth(const String &chal, const String &uri) {
  String realm = headerField(chal, "realm");
  String nonce = headerField(chal, "nonce");
  String qop = headerField(chal, "qop");
  if (realm.length() == 0 || nonce.length() == 0) return "";

  String cnonce = String(millis(), HEX);
  String nc = "00000001";
  String ha1 = md5Hex(serial_ + ":" + realm + ":" + key_);
  String ha2 = md5Hex("GET:" + uri);
  String resp = md5Hex(ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" +
                       qop + ":" + ha2);

  return "Digest username=\"" + serial_ + "\", realm=\"" + realm +
         "\", nonce=\"" + nonce + "\", uri=\"" + uri +
         "\", qop=" + qop + ", nc=" + nc + ", cnonce=\"" + cnonce +
         "\", response=\"" + resp + "\"";
}

// Performs the 401-then-retry Digest exchange. Returns the body on
// success and "" on any failure. Arduino's HTTPClient only implements
// Basic auth, which is why this is done by hand.
static String digestGet(const String &host, const String &uri,
                        String *asnOut) {
  NetworkClientSecure client;
  client.setInsecure(); // a pinned CA root would expire and silently break
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

void eddiPump() {
  if (serial_.length() == 0 || key_.length() == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastPollAtMs < EDDI_POLL_INTERVAL_MS) return;
  lastPollAtMs = millis();

  // Discover the hub's server once per boot. The director answers with
  // an X_MYENERGI-asn header naming it; if that fails, the known host is
  // used rather than giving up.
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
  //
  // Two cases are deliberately silent. The first successful fetch after
  // boot has nothing to compare against, so without this every power cut
  // would announce itself. And a transition out of EDDI_UNKNOWN means the
  // device has just regained contact, not that the heater did anything:
  // treating a network blip as an Eddi event would produce a false alert
  // on every outage.
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
