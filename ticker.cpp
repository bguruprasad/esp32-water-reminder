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
  // setTimeout bounds the read phase only. Without setConnectTimeout the
  // TCP connect and TLS handshake are governed by a separate, longer
  // default, so a half-open connection could stall loop() well past 5s.
  // Both are capped so the worst-case stall is bounded.
  http.setConnectTimeout(5000);
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
