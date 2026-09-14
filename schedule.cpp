#include "schedule.h"
#include "config.h"

// Compares two struct tm by their time-of-day + date, via mktime's
// underlying representation (safe for same-session comparisons since
// we never cross a TZ change mid-comparison here).
static time_t toEpoch(const struct tm &t) {
  struct tm copy = t;
  return mktime(&copy);
}

bool scheduleIsWithinWindow(const struct tm &nowLocal) {
#ifdef DEBUG_FAST_SCHEDULE
  return true;
#else
  bool isWeekday = (nowLocal.tm_wday >= 1 && nowLocal.tm_wday <= 5); // Mon-Fri
  bool isWorkHour = (nowLocal.tm_hour >= 9 && nowLocal.tm_hour < 18);
  return isWeekday && isWorkHour;
#endif
}

struct tm scheduleNextMark(const struct tm &nowLocal, const struct tm &lastFiredMark) {
#ifdef DEBUG_FAST_SCHEDULE
  const int intervalSeconds = 30;
  time_t lastEpoch = toEpoch(lastFiredMark);
  time_t nextEpoch = lastEpoch + intervalSeconds;
  struct tm result;
  localtime_r(&nextEpoch, &result);
  return result;
#else
  const int intervalMinutes = 30;
  time_t lastEpoch = toEpoch(lastFiredMark);
  time_t nextEpoch = lastEpoch + (intervalMinutes * 60);
  struct tm result;
  localtime_r(&nextEpoch, &result);
  return result;
#endif
}

bool scheduleIsMarkDue(const struct tm &nowLocal, const struct tm &lastFiredMark) {
  if (!scheduleIsWithinWindow(nowLocal)) {
    return false;
  }
  struct tm nextMark = scheduleNextMark(nowLocal, lastFiredMark);
  return toEpoch(nowLocal) >= toEpoch(nextMark);
}
