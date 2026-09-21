#include "chart.h"
#include "ticker.h"
#include "wifi_setup.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>

static const int CHART_MAX_POINTS = 32;   // Yahoo returns 27 at 1d/15m
static const int CHART_MAX_CANDLES = 30;  // the last 30 minutes at 1m
static const int CHART_MAX_SYMBOLS = 8;

// 15s rather than 30s because there are now two series per symbol. At
// 30s the pair would refresh every 8 minutes, and a chart labelled "last
// 30 minutes" that is 8 minutes stale misrepresents itself by a quarter
// of its own window. At 15s both refresh every 4 minutes, costing 4
// requests a minute to a keyless endpoint.
static const unsigned long CHART_FETCH_INTERVAL_MS = 15000;

struct Series {
  float closes[CHART_MAX_POINTS];
  int count = 0;
  float lo = 0, hi = 0, prevClose = 0;
};

struct Candles {
  float open[CHART_MAX_CANDLES];
  float high[CHART_MAX_CANDLES];
  float low[CHART_MAX_CANDLES];
  float close[CHART_MAX_CANDLES];
  int count = 0;
  float lo = 0, hi = 0;
};

static Series series[CHART_MAX_SYMBOLS];
static Candles candles[CHART_MAX_SYMBOLS];
static int nextIndex = 0;
static bool fetchCandlesNext = false; // alternates with the day line
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

int chartCandleCount(int symbolIndex) {
  if (symbolIndex < 0 || symbolIndex >= CHART_MAX_SYMBOLS) return 0;
  return candles[symbolIndex].count;
}

bool chartCandle(int symbolIndex, int i, float &openOut, float &highOut,
                 float &lowOut, float &closeOut) {
  if (symbolIndex < 0 || symbolIndex >= CHART_MAX_SYMBOLS) return false;
  const Candles &c = candles[symbolIndex];
  if (i < 0 || i >= c.count) return false;
  openOut = c.open[i]; highOut = c.high[i];
  lowOut = c.low[i]; closeOut = c.close[i];
  return true;
}

bool chartCandleRange(int symbolIndex, float &loOut, float &hiOut) {
  if (symbolIndex < 0 || symbolIndex >= CHART_MAX_SYMBOLS) return false;
  const Candles &c = candles[symbolIndex];
  if (c.count < 2 || c.hi <= c.lo) return false;
  loOut = c.lo; hiOut = c.hi;
  return true;
}

bool chartCandleOpen(int symbolIndex, float &openOut) {
  if (symbolIndex < 0 || symbolIndex >= CHART_MAX_SYMBOLS) return false;
  const Candles &c = candles[symbolIndex];
  if (c.count < 2) return false;
  openOut = c.open[0];
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

// Parses the 1-minute response, keeping only the LAST CHART_MAX_CANDLES
// rows. The payload is about 22KB for a full session, so this reads
// through everything and retains a rolling tail rather than buffering
// it: the point of this series is the most recent half hour.
//
// A candle needs all four of open/high/low/close. Yahoo scatters nulls
// through these arrays independently, so a row missing any one of them
// is skipped entirely rather than partially filled, which would
// otherwise draw a body or wick from a stale or zero value.
static bool parseCandles(const String &body, Candles &out) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) return false;

  JsonObject result = doc["chart"]["result"][0];
  if (result.isNull()) return false;

  JsonObject q = result["indicators"]["quote"][0];
  JsonArray o = q["open"], h = q["high"], l = q["low"], c = q["close"];
  if (o.isNull() || h.isNull() || l.isNull() || c.isNull()) return false;

  const size_t total = c.size();
  int n = 0;
  for (size_t i = 0; i < total; i++) {
    if (o[i].isNull() || h[i].isNull() || l[i].isNull() || c[i].isNull()) continue;

    if (n == CHART_MAX_CANDLES) {
      // Full: drop the oldest so the window stays on the latest rows.
      for (int k = 1; k < CHART_MAX_CANDLES; k++) {
        out.open[k - 1] = out.open[k];
        out.high[k - 1] = out.high[k];
        out.low[k - 1]  = out.low[k];
        out.close[k - 1] = out.close[k];
      }
      n = CHART_MAX_CANDLES - 1;
    }

    out.open[n]  = o[i].as<float>();
    out.high[n]  = h[i].as<float>();
    out.low[n]   = l[i].as<float>();
    out.close[n] = c[i].as<float>();
    n++;
  }
  if (n < 2) return false;

  // Scale from the high and low columns, not the closes, so a wick can
  // never be drawn outside the plotted band.
  float lo = out.low[0], hi = out.high[0];
  for (int i = 1; i < n; i++) {
    if (out.low[i] < lo) lo = out.low[i];
    if (out.high[i] > hi) hi = out.high[i];
  }
  if (hi <= lo) return false;

  out.count = n; out.lo = lo; out.hi = hi;
  return true;
}

// A failed fetch leaves the previous series untouched, so the panel keeps
// showing the last good chart rather than blanking. The endpoint is
// undocumented, so every failure path here is a no-op by design.
static void fetchSeries(int index, bool wantCandles) {
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
  url += wantCandles ? "?range=1d&interval=1m" : "?range=1d&interval=15m";

  if (!http.begin(client, url)) return;
  http.setUserAgent("ESP32HTTPClient"); // a browser UA gets rate limited

  if (http.GET() == 200) {
    String payload = http.getString();
    if (wantCandles) {
      Candles parsed;
      if (parseCandles(payload, parsed)) candles[index] = parsed;
    } else {
      Series parsed;
      if (parseChart(payload, parsed)) series[index] = parsed;
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

  // Fetch only what the display can actually show. Pulling both series
  // when only one is drawn doubles the round trip for nothing, and it
  // is the refresh rate that pays: with one series the whole symbol
  // list comes round in 2 minutes rather than 4, which matters most for
  // the candles, whose window is only 30 minutes wide.
  const int pref = wifiSetupChartMode();

  if (pref == 1) {          // candles only
    fetchSeries(nextIndex, true);
    nextIndex = (nextIndex + 1) % n;
    return;
  }
  if (pref == 2) {          // line only
    fetchSeries(nextIndex, false);
    nextIndex = (nextIndex + 1) % n;
    return;
  }

  fetchSeries(nextIndex, fetchCandlesNext);

  // Alternate kind first, advancing the symbol only after both series
  // for it have been fetched, so a symbol never has one series far
  // fresher than the other.
  if (fetchCandlesNext) {
    fetchCandlesNext = false;
    nextIndex = (nextIndex + 1) % n;
  } else {
    fetchCandlesNext = true;
  }
}
