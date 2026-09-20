#include "config.h"
#include "wifi_setup.h"
#include "time_sync.h"
#include "schedule.h"
#include "ticker.h"
#include "chart.h"
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

// Chart panel state. Two symbols are shown at a time, advancing to the
// next pair every PANEL_PAGE_MS.
//
// The panels are repainted only when the pair actually changes - once
// per 5s - which is the same repaint-on-change discipline that fixed the
// clock flicker and, before that, the scrolling band's flicker.
//
// Unlike the old band, the panels are NOT hidden outside market hours:
// Yahoo keeps serving the last session, so there is always a chart to
// show. Only the Finnhub price fetching pauses when the market is shut.
static const unsigned long PANEL_PAGE_MS = 5000;
static unsigned long lastPanelPageMs = 0;
static int panelPair = 0;
static bool panelsVisible = false;

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
  // Portrait. 0 and 2 are both portrait, 180 degrees apart. Confirmed on
  // hardware: 0 put the clock at the end away from the USB connector, so
  // 2 is the one that stands the board upright with the cable at the
  // bottom. Display and touch must always use the same value, or taps
  // land mirrored through both axes.
  tft.setRotation(2);
  touch.setRotation(2);
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
  chartBegin();
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

    tickerPump(); // prices, Finnhub, market hours only (checked internally)
    chartPump();  // candles, Yahoo, at most one symbol per 30s

    // No market-hours gate here: Yahoo keeps serving the last session, so
    // there is always a chart worth showing. Only price fetching pauses.
    int symbolCount = tickerSymbolCount();
    int pairCount = symbolCount > 0 ? (symbolCount + 1) / 2 : 0;

    if (pairCount == 0) {
      if (panelsVisible) { uiClearChartPanels(tft); panelsVisible = false; }
    } else if (!panelsVisible) {
      // First paint, or returning from an alert that wiped the screen:
      // draw immediately rather than waiting out a full dwell blank.
      lastPanelPageMs = millis();
      uiDrawChartPanel(tft, 0, panelPair * 2);
      uiDrawChartPanel(tft, 1, panelPair * 2 + 1);
      panelsVisible = true;
    } else if (millis() - lastPanelPageMs >= PANEL_PAGE_MS) {
      // Dwell elapsed: advance to the next pair. This is the ONLY place
      // the panels are repainted during steady state.
      lastPanelPageMs = millis();
      panelPair = (panelPair + 1) % pairCount;
      uiDrawChartPanel(tft, 0, panelPair * 2);
      uiDrawChartPanel(tft, 1, panelPair * 2 + 1);
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
      // Jump to the most recent grid mark at or before now, rather than
      // advancing a single step from the previous value.
      //
      // Stepping by one interval only worked if no mark had been missed.
      // If the device sat idle across several marks (asleep, off, or just
      // outside the window), lastFiredMark was hours stale, so advancing
      // it by 30 minutes left it STILL in the past - scheduleIsMarkDue()
      // fired again on the very next loop pass, and the alert reappeared
      // the instant it was dismissed, once per missed half-hour.
      //
      // Flooring to now collapses any backlog into this one dismissal
      // while keeping the :00/:30 grid alignment.
      lastFiredMark = scheduleFloorToMark(nowLocal);
      appState = STATE_IDLE;
      uiDrawIdleScreen(tft, nowLocal, buildStatusLine(nowLocal));
      panelsVisible = false; // full repaint wiped the panels; let them redraw
    } else if (millis() - lastFlashStepAtMs >= ALERT_FLASH_STEP_MS) {
      lastFlashStepAtMs = millis();
      alertColor = nextAlertColor(alertColor);
      uiDrawAlertScreen(tft, alertColor);
    }
  }
}
