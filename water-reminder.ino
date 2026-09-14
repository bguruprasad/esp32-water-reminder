#include "config.h"
#include "wifi_setup.h"
#include "time_sync.h"
#include "ui.h"
#include "schedule.h"
#include <TFT_eSPI.h>
#include <TFT_Touch.h>
#include <time.h>

TFT_eSPI tft = TFT_eSPI();
TFT_Touch touch = TFT_Touch(TOUCH_DCS, TOUCH_DCLK, TOUCH_DIN, TOUCH_DOUT);

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
}

void loop() {
  static int lastDrawnSecond = -1;

  time_t now = time(nullptr);
  struct tm nowLocal;
  localtime_r(&now, &nowLocal);

  if (nowLocal.tm_sec != lastDrawnSecond) {
    lastDrawnSecond = nowLocal.tm_sec;
    String status = timeSyncIsValid() ? "" : "Time not synced";
    uiDrawIdleScreen(tft, nowLocal, status);
  }
}
