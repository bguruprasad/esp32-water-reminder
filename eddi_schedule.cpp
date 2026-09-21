#include "eddi_schedule.h"
#include "eddi.h"
#include "notify.h"
#include "wifi_setup.h"

// How long to wait after issuing a command before checking whether the
// device moved, and how many times to re-issue before giving up.
//
// The cloud accepts a command immediately but the physical device can
// take 3 to 5 minutes to act, reporting the old status throughout. Five
// attempts two minutes apart gives a 10 minute window. The controller
// this replaces saw 22:00 confirm on its third attempt, about 4 minutes
// in, so 10 minutes has real margin.
static const int VERIFY_ATTEMPTS = 5;
static const unsigned long VERIFY_WAIT_MS = 120000;

static int lastMinuteOfDay = -1;   // -1 until the first pump after boot
static bool verifyActive = false;
static bool wantStart = false;
static int attempt = 0;
static unsigned long lastAttemptMs = 0;

void eddiScheduleBegin() {
  lastMinuteOfDay = -1;
  verifyActive = false;
}

static String hhMm(const struct tm &t) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  return String(buf);
}

// True if target lies in (prev, now]. Handles the midnight wrap, where
// now is numerically smaller than prev.
static bool crossed(int prev, int now, int target) {
  if (prev == now) return false;
  if (prev < now) return target > prev && target <= now;
  return target > prev || target <= now;
}

// Starts a command and the verification that follows it. The command is
// issued immediately; whether it took effect is decided later by
// watching the status the eddi module already polls.
static void beginCommand(bool start, const struct tm &nowLocal) {
  wantStart = start;
  verifyActive = true;
  attempt = 1;
  lastAttemptMs = millis();

  bool accepted = eddiSendMode(start);
  if (!accepted) {
    // Rejected outright: no point waiting to see if it took effect.
    verifyActive = false;
    notifySend(String("Eddi: ") + (start ? "start" : "stop") +
               " was REJECTED by the API at " + hhMm(nowLocal) +
               ". Nothing changed. Check the app.");
  }
}

// Watches the status the eddi module is already fetching. Nothing here
// makes a request of its own, so verification costs no API traffic and
// cannot push the device past the rate guidance.
static void pumpVerify(const struct tm &nowLocal) {
  if (!verifyActive) return;
  if (millis() - lastAttemptMs < VERIFY_WAIT_MS) return;

  int sta = eddiStatus();

  // Unknown means the poll itself is failing, which says nothing about
  // whether the heater moved. Wait rather than count it as a failure.
  if (sta == EDDI_UNKNOWN) { lastAttemptMs = millis(); return; }

  bool reached = wantStart ? (sta != 6) : (sta == 6);
  if (reached) {
    verifyActive = false;
    notifySend(String("Eddi: ") + (wantStart ? "started" : "stopped") +
               " and confirmed at " + hhMm(nowLocal) +
               " (now " + eddiStatusText(sta) + ")");
    return;
  }

  if (attempt >= VERIFY_ATTEMPTS) {
    verifyActive = false;
    notifySend(String("Eddi: ") + (wantStart ? "start" : "stop") +
               " NOT confirmed after " + String(VERIFY_ATTEMPTS) +
               " attempts. Still " + eddiStatusText(sta) +
               ". Giving up, check the app.");
    return;
  }

  attempt++;
  lastAttemptMs = millis();
  eddiSendMode(wantStart);
}

void eddiSchedulePump(const struct tm &nowLocal) {
  int nowMin = nowLocal.tm_hour * 60 + nowLocal.tm_min;

  // The first pump after boot only records where the clock is. Without
  // this, a device booting at exactly 12:00 would treat the current
  // minute as a crossing and issue a command it never observed
  // happening - which is the catch-up behaviour this deliberately does
  // not do.
  if (lastMinuteOfDay < 0) {
    lastMinuteOfDay = nowMin;
    return;
  }

  if (nowMin != lastMinuteOfDay) {
    int prev = lastMinuteOfDay;
    lastMinuteOfDay = nowMin;

    // A command already in flight is not interrupted. The two scheduled
    // times are ten hours apart, so this only matters if someone sets
    // them minutes apart, where finishing the first is the sane choice.
    if (!verifyActive) {
      if (crossed(prev, nowMin, wifiSetupEddiStopMin())) {
        beginCommand(false, nowLocal);
      } else if (crossed(prev, nowMin, wifiSetupEddiStartMin())) {
        beginCommand(true, nowLocal);
      }
    }
  }

  pumpVerify(nowLocal);
}
