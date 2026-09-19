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
// showing the last good chart rather than blanking. The endpoint is
// undocumented, so every failure path here is a no-op by design.
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
