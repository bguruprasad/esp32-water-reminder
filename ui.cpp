#include "ui.h"
#include "config.h"

static const char *WEEKDAY_NAMES[7] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

void uiDrawIdleScreen(TFT_eSPI &tft, const struct tm &nowLocal, const String &statusLine) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  char timeBuf[9];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
           nowLocal.tm_hour, nowLocal.tm_min, nowLocal.tm_sec);
  tft.drawString(timeBuf, TFT_HRES / 2, TFT_VRES / 2 - 20, 4);

  const char *weekday = (nowLocal.tm_wday >= 0 && nowLocal.tm_wday < 7)
                          ? WEEKDAY_NAMES[nowLocal.tm_wday] : "?";
  tft.drawString(weekday, TFT_HRES / 2, TFT_VRES / 2 + 20, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(statusLine, TFT_HRES / 2, TFT_VRES / 2 + 45, 2);
}
