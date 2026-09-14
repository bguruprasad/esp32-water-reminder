#include "config.h"
#include "wifi_setup.h"
#include "time_sync.h"
#include "schedule.h"
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
}

void loop() {
  time_t now = time(nullptr);
  struct tm nowLocal;
  localtime_r(&now, &nowLocal);

  if (appState == STATE_IDLE) {
    static int lastDrawnSecond = -1;
    if (nowLocal.tm_sec != lastDrawnSecond) {
      lastDrawnSecond = nowLocal.tm_sec;
      uiDrawIdleScreen(tft, nowLocal, buildStatusLine(nowLocal));
    }

    if (timeSyncIsValid() && lastFiredMarkInitialized &&
        scheduleIsMarkDue(nowLocal, lastFiredMark)) {
      appState = STATE_ALERT;
      uiDrawAlertScreen(tft);
    }
  } else { // STATE_ALERT
    if (touch.Pressed()) {
      delay(50); // debounce, matches pattern from Freenove touch examples
      // The mark that just fired is scheduleNextMark() of the PREVIOUS
      // lastFiredMark — not nowLocal (the tap time) — so the grid stays
      // aligned to :00/:30 regardless of how long the alert was showing
      // before the user tapped.
      lastFiredMark = scheduleNextMark(nowLocal, lastFiredMark);
      appState = STATE_IDLE;
      uiDrawIdleScreen(tft, nowLocal, buildStatusLine(nowLocal));
    }
  }
}
