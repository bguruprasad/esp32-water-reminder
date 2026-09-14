#include "time_sync.h"
#include "config.h"
#include <time.h>

bool timeSyncIsValid() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  return (timeinfo.tm_year + 1900) > 2020;
}

bool timeSyncStart() {
  configTzTime(WATER_REMINDER_TZ, "pool.ntp.org", "time.nist.gov");

  unsigned long start = millis();
  while (!timeSyncIsValid() && millis() - start < 10000) {
    delay(200);
  }

  return timeSyncIsValid();
}
