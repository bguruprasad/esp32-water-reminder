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
// The status as it was when the command went out, so a start can be
// confirmed by a real transition rather than by a state that was
// already true before anything was sent.
static int staBefore = EDDI_UNKNOWN;

void eddiScheduleBegin() {
  lastMinuteOfDay = -1;
  verifyActive = false;
}

static String hhMm(const struct tm &t) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  return String(buf);
}

// The largest gap between two observations still treated as the clock
// simply ticking. A fetch can stall the loop for 5 seconds and an alert
// holds it for 30, so a few minutes is generous; beyond that it is a
// stall or a clock correction, not time passing under observation.
static const int MAX_GAP_MINUTES = 5;

// True if target lies in (prev, now], where now is the FIRST observation
// after prev and the two are close enough together to be consecutive.
//
// The gap bound is the whole point of this function, and its absence
// fired a scheduled 12:00 stop at 01:12. The old version treated any
// prev > now as "midnight was crossed" and then counted every target
// above prev, so a jump from 11:40 to 01:12 - a stall, a reconnect, or
// an NTP correction - was indistinguishable from a one-minute tick past
// midnight, and swept up the 12:00 target on the way through.
//
// Refusing a crossing across a large gap is the same rule the rest of
// the scheduler follows: a scheduled time the device did not actually
// observe passing is skipped, never caught up, because it cannot tell a
// command missed while it was away from one the owner has since
// deliberately overridden.
static bool crossed(int prev, int now, int target) {
  if (prev == now) return false;

  if (prev < now) {
    if (now - prev > MAX_GAP_MINUTES) return false;
    return target > prev && target <= now;
  }

  // now < prev: either a real midnight wrap, or the clock moved by
  // hours. Only the former is a tick.
  const int wrapGap = (1440 - prev) + now;
  if (wrapGap > MAX_GAP_MINUTES) return false;
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
  staBefore = eddiStatus();

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
  // whether the heater moved. Waiting is right, but the attempt still
  // has to count: without it, give-up was unreachable, verifyActive
  // stuck true forever, and because eddiSchedulePump() will not start a
  // command while one is in flight, a sustained API outage silently
  // blocked every future stop and start until the device was rebooted.
  if (sta == EDDI_UNKNOWN) {
    lastAttemptMs = millis();
    if (++attempt > VERIFY_ATTEMPTS) {
      verifyActive = false;
      notifySend(String("Eddi: ") + (wantStart ? "start" : "stop") +
                 " could not be checked at " + hhMm(nowLocal) +
                 ". The status has been unreachable throughout. Check the app.");
    }
    return;
  }

  // A start is confirmed by an actual transition out of Stopped, not by
  // the mere absence of Stopped. The Eddi is usually Paused, which is
  // already "not 6", so the old check self-confirmed against whatever
  // the status happened to be and would have reported success even if
  // the command had been dropped entirely.
  bool reached = wantStart ? (staBefore == 6 && sta != 6) : (sta == 6);

  // Nothing to transition from: a start commanded while already running,
  // or a stop while already stopped. The command was still sent, but no
  // change can prove it landed, so say that rather than claim success.
  bool alreadyThere = wantStart ? (staBefore != 6) : (staBefore == 6);
  if (alreadyThere) {
    verifyActive = false;
    notifySend(String("Eddi: ") + (wantStart ? "start" : "stop") +
               " sent at " + hhMm(nowLocal) + ", but it was already " +
               eddiStatusText(staBefore) + " so there was nothing to confirm.");
    return;
  }
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
