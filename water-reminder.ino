#include "config.h"
#include "wifi_setup.h"
#include "time_sync.h"
#include "schedule.h"
#include "ticker.h"
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

// Alert flash/dismiss state. The alert cycles background colors every
// ALERT_FLASH_STEP_MS and auto-dismisses after ALERT_DURATION_MS if not
// tapped first.
static const unsigned long ALERT_DURATION_MS = 30000;
static const unsigned long ALERT_FLASH_STEP_MS = 400;
static unsigned long alertStartedAtMs = 0;
static unsigned long lastFlashStepAtMs = 0;
static AlertFlashColor alertColor = ALERT_RED;

// Ticker band scroll state. The band advances a few pixels per frame
// while the market is open; it is hidden entirely when shut.
static const unsigned long TICKER_FRAME_MS = 40; // ~25fps
static const int TICKER_SCROLL_STEP_PX = 2;
static unsigned long lastTickerFrameMs = 0;
static int tickerScrollPx = 0;
static bool tickerBandVisible = false;

static AlertFlashColor nextAlertColor(AlertFlashColor c) {
  switch (c) {
    case ALERT_RED:   return ALERT_AMBER;
    case ALERT_AMBER: return ALERT_GREEN;
    case ALERT_GREEN: return ALERT_RED;
    default:           return ALERT_RED;
  }
}

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

  // Initialize lastFiredMark to the most recent grid-aligned mark (floor
  // of boot time to :00/:30), so the device waits for the next grid mark
  // rather than firing immediately on boot, and stays on the 09:00/09:30
  // grid regardless of what time it happened to boot at.
  time_t now = time(nullptr);
  struct tm nowAtBoot;
  localtime_r(&now, &nowAtBoot);
  lastFiredMark = scheduleFloorToMark(nowAtBoot);
  lastFiredMarkInitialized = true;

  tickerBegin();
}

void loop() {
  time_t now = time(nullptr);
  struct tm nowLocal;
  localtime_r(&now, &nowLocal);

  if (appState == STATE_IDLE) {
    // Partial update: repaints only the parts that actually changed, so
    // there is no full-screen clear (and therefore no flicker) on the
    // ticks where nothing is different. The clock shows no seconds, so
    // in practice this redraws about once a minute.
    uiUpdateIdleScreen(tft, nowLocal, buildStatusLine(nowLocal));

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

    if (timeSyncIsValid() && lastFiredMarkInitialized &&
        scheduleIsMarkDue(nowLocal, lastFiredMark)) {
      appState = STATE_ALERT;
      alertColor = ALERT_RED;
      alertStartedAtMs = millis();
      lastFlashStepAtMs = alertStartedAtMs;
      uiDrawAlertScreen(tft, alertColor);
    }
  } else { // STATE_ALERT
    unsigned long elapsedMs = millis() - alertStartedAtMs;
    bool tapped = touch.Pressed();
    bool timedOut = elapsedMs >= ALERT_DURATION_MS;

    if (tapped || timedOut) {
      if (tapped) {
        delay(50); // debounce, matches pattern from Freenove touch examples
      }
      // The mark that just fired is scheduleNextMark() of the PREVIOUS
      // lastFiredMark — not nowLocal (the tap time) — so the grid stays
      // aligned to :00/:30 regardless of how long the alert was showing
      // before it was dismissed (by tap or timeout).
      lastFiredMark = scheduleNextMark(nowLocal, lastFiredMark);
      appState = STATE_IDLE;
      uiDrawIdleScreen(tft, nowLocal, buildStatusLine(nowLocal));
      tickerBandVisible = false; // full repaint wiped the band; let it redraw
    } else if (millis() - lastFlashStepAtMs >= ALERT_FLASH_STEP_MS) {
      lastFlashStepAtMs = millis();
      alertColor = nextAlertColor(alertColor);
      uiDrawAlertScreen(tft, alertColor);
    }
  }
}
