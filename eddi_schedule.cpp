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

// True when a target lies inside a gap too large to count as a tick, so
// crossed() refused it. Skipping is deliberate - a time the device did
// not watch pass cannot be told apart from one the owner has since
// overridden - but skipping SILENTLY is not: an unexplained gap is how
// the 01:12 misfire went unnoticed for two days. Saying so costs one
// message and turns an invisible non-event into something checkable.
static bool gapSkipped(int prev, int now, int target) {
  if (prev == now) return false;
  const int gap = (prev < now) ? (now - prev) : ((1440 - prev) + now);
  if (gap <= MAX_GAP_MINUTES) return false;  // crossed() handled it
  if (prev < now) return target > prev && target <= now;
  return target > prev || target <= now;
}

// Minutes since midnight as "HH:MM", for the messages above.
static String minutesToHhMm(int mins) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", mins / 60, mins % 60);
  return String(buf);
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
    // Re-issue as well as count. Re-sending a possibly dropped command
    // is the retry loop's entire purpose, and a link flaky enough to
    // lose status polls is exactly the one likely to have lost the
    // command too. Without this, the same outage that hid the status
    // also guaranteed the command was never retried.
    if (attempt <= VERIFY_ATTEMPTS) eddiSendMode(wantStart);
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
  // already "not 6", so a bare "sta != 6" self-confirmed against
  // whatever the status happened to be and would have reported success
  // even if the command had been dropped entirely.
  //
  // When the status before the command is unknown, there is no
  // transition to measure, so fall back to the commanded state alone.
  // That is weaker, but it is the honest reading of what we know, and
  // it keeps the retry loop running. Treating unknown as "already
  // there" would skip every retry: EDDI_UNKNOWN is -1, which satisfies
  // "!= 6", so a start issued while the first status poll had not yet
  // landed would be dismissed as needing no confirmation and the heater
  // would stay stopped all day.
  const bool knewBefore = (staBefore != EDDI_UNKNOWN);
  bool reached;
  if (wantStart) {
    reached = knewBefore ? (staBefore == 6 && sta != 6) : (sta != 6);
  } else {
    reached = (sta == 6);
  }

  // Reaching the commanded state is checked FIRST. A stop issued while
  // the Eddi was already Stopped satisfies both this and the "nothing to
  // transition from" case below, and the device being verifiably in the
  // state that was asked for is the more useful thing to report.
  if (reached) {
    verifyActive = false;
    notifySend(String("Eddi: ") + (wantStart ? "started" : "stopped") +
               " and confirmed at " + hhMm(nowLocal) +
               " (now " + eddiStatusText(sta) + ")");
    return;
  }

  // Nothing to transition from, and the commanded state was not reached:
  // a start while already running. The command went out, but no change
  // can prove it landed, so say that rather than claim a confirmation.
  bool alreadyThere = knewBefore && wantStart && staBefore != 6;
  if (alreadyThere) {
    verifyActive = false;
    notifySend(String("Eddi: ") + (wantStart ? "start" : "stop") +
               " sent at " + hhMm(nowLocal) + ", but it was already " +
               eddiStatusText(staBefore) + " so there was nothing to confirm.");
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
      const int stopMin = wifiSetupEddiStopMin();
      const int startMin = wifiSetupEddiStartMin();

      if (crossed(prev, nowMin, stopMin)) {
        beginCommand(false, nowLocal);
      } else if (crossed(prev, nowMin, startMin)) {
        beginCommand(true, nowLocal);
      } else if (gapSkipped(prev, nowMin, stopMin)) {
        notifySend(String("Eddi: the ") + minutesToHhMm(stopMin) +
                   " stop was skipped. The clock jumped from " +
                   minutesToHhMm(prev) + " to " + minutesToHhMm(nowMin) +
                   ", so the device never saw it pass. Nothing was sent.");
      } else if (gapSkipped(prev, nowMin, startMin)) {
        notifySend(String("Eddi: the ") + minutesToHhMm(startMin) +
                   " start was skipped. The clock jumped from " +
                   minutesToHhMm(prev) + " to " + minutesToHhMm(nowMin) +
                   ", so the device never saw it pass. Nothing was sent.");
      }
    }
  }

  pumpVerify(nowLocal);
}
