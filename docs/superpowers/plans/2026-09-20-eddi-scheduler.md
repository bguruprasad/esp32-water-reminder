# Eddi Scheduler Implementation Plan (phase 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the device the Raspberry Pi's job: stop the Eddi at 12:00 and start it at 22:00 every day, verify each command actually took effect, and report the outcome, so the Pi can be retired.

**Architecture:** The existing `eddi` module gains a command path alongside its status polling. A small scheduler decides when a scheduled time has been crossed; a verifier watches the status the module is already fetching and re-issues if the device has not moved. Times live in NVS, entered on the setup page.

**Tech Stack:** No new dependencies. The Digest GET proven in phase 1, `Preferences`, and the existing 30 second poll.

**Spec:** [docs/superpowers/specs/2026-09-20-eddi-status-design.md](../specs/2026-09-20-eddi-status-design.md), phase 2 addendum.

## Global Constraints

- Board: FQBN `esp32:esp32:esp32`, port `/dev/cu.usbserial-1120` (the trailing number changes between replugs; check `ls /dev/cu.usb*`), upload speed `115200`.
- **`PartitionScheme=no_fs` on every compile and upload.** A default-scheme flash erases saved credentials.
- **No em dashes and no non-ASCII** in any tracked file or commit message. Verify with Python, never `grep -P`.
- **No `Co-Authored-By` trailers.**
- **No secret in any file, commit, or conversation.**
- Poll and command traffic together must stay within one request per 10 seconds. The status poll is every 30s; commands are rare, but a verify cycle must not tighten the interval.
- Default schedule: **stop 12:00, start 22:00**, local Dublin time, every day.
- A missed command is **never** re-issued after a reboot. See the spec's decisions section.
- NVS key names are 15 characters or fewer.

---

### Task 1: Schedule storage and setup page fields

**Files:**
- Modify: `wifi_setup.cpp`, `wifi_setup.h`

**Interfaces:**
- Produces: `int wifiSetupEddiStopMin();` and `int wifiSetupEddiStartMin();`, each returning minutes since midnight (0-1439), defaulting to 720 (12:00) and 1320 (22:00) when unset.

- [ ] **Step 1: Add NVS keys and defaults**

```cpp
static const char *NVS_KEY_EDSTOP = "edstop";
static const char *NVS_KEY_EDSTART = "edstart";
static const int EDDI_DEFAULT_STOP_MIN = 12 * 60;  // 12:00
static const int EDDI_DEFAULT_START_MIN = 22 * 60; // 22:00
```

- [ ] **Step 2: Add two time fields to the form**

Use `type='time'`, which gives a native time picker on a phone and posts `HH:MM`. Prefill from the stored value, since these are not secrets.

```cpp
h += "Eddi stop time:<br><input name='edstop' type='time' value=\"" +
     minutesToHhMm(readSettingInt(NVS_KEY_EDSTOP, EDDI_DEFAULT_STOP_MIN)) +
     "\"><br>";
h += "Eddi start time:<br><input name='edstart' type='time' value=\"" +
     minutesToHhMm(readSettingInt(NVS_KEY_EDSTART, EDDI_DEFAULT_START_MIN)) +
     "\"><br>";
h += "<small>Local time, every day. Leave as-is for 12:00 and 22:00.</small><br><br>";
```

- [ ] **Step 3: Add the two helpers**

```cpp
// "HH:MM" as posted by an <input type=time>, to minutes since midnight.
// Returns fallback on anything malformed, so a blank or odd submission
// cannot leave the schedule in an undefined state.
static int hhMmToMinutes(const String &s, int fallback) {
  int colon = s.indexOf(':');
  if (colon != 2 || s.length() < 5) return fallback;
  int h = s.substring(0, 2).toInt();
  int m = s.substring(3, 5).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return h * 60 + m;
}

static String minutesToHhMm(int mins) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", mins / 60, mins % 60);
  return String(buf);
}
```

- [ ] **Step 4: Persist and expose them**

Read both in `handleSave()` through `hhMmToMinutes`, store as strings via the existing `putString`, and add the two getters returning minutes.

- [ ] **Step 5: Compile, flash, verify**

Reach the portal with the boot touch gesture, confirm both time fields show 12:00 and 22:00, change one, reboot, confirm it persisted and that WiFi and the keys were untouched.

- [ ] **Step 6: Commit**

```bash
git add wifi_setup.cpp wifi_setup.h
git commit -m "Store an Eddi stop and start time in NVS"
```

---

### Task 2: The command path

**Files:**
- Modify: `eddi.cpp`, `eddi.h`

**Interfaces:**
- Produces: `bool eddiSendMode(bool start);` issuing the Digest GET and returning whether the API accepted it. Acceptance is not confirmation; Task 3 handles that.

- [ ] **Step 1: Add the command function**

Reuses `digestGet` and the cached `host_` exactly as the status fetch does.

```cpp
// Issues a mode change. A 200 means the cloud ACCEPTED the command, not
// that the heater has acted on it: propagation takes 3 to 5 minutes and
// the status keeps reporting the old value throughout. Confirmation is
// the scheduler's job, not this function's.
bool eddiSendMode(bool start) {
  if (serial_.length() == 0 || key_.length() == 0) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  if (host_.length() == 0) return false; // no server discovered yet

  String uri = "/cgi-eddi-mode-E" + serial_ + (start ? "-1" : "-0");
  String body = digestGet(host_, uri, nullptr);
  return body.length() > 0;
}
```

- [ ] **Step 2: Declare it in `eddi.h`**

With a comment making the accepted-versus-confirmed distinction explicit, since that is the single most misleading thing about this API.

- [ ] **Step 3: Compile and flash**

No behaviour change yet: nothing calls it. Verify the pill and everything else still work.

- [ ] **Step 4: Verify the command actually works, once, by hand**

Temporarily call `eddiSendMode(false)` from `setup()`, flash, and confirm in the myenergi app that the Eddi stops. Then restore the previous mode from the app, remove the temporary call, and flash again. Do not commit the temporary call.

This is the same isolate-and-prove approach used for Telegram in phase 1, and it matters more here: this is the first code in the project that changes something physical in the house.

- [ ] **Step 5: Commit**

```bash
git add eddi.cpp eddi.h
git commit -m "Add the Eddi mode command path"
```

---

### Task 3: The scheduler and verifier

**Files:**
- Create: `eddi_schedule.cpp`, `eddi_schedule.h`
- Modify: `water-reminder.ino`

**Interfaces:**
- Consumes: `wifiSetupEddiStopMin()`, `wifiSetupEddiStartMin()`, `eddiSendMode()`, `eddiStatus()`, `notifySend()`.
- Produces: `void eddiScheduleBegin();` and `void eddiSchedulePump(const struct tm &nowLocal);`.

- [ ] **Step 1: Write the header**

```cpp
#ifndef EDDI_SCHEDULE_H
#define EDDI_SCHEDULE_H

#include <Arduino.h>
#include <time.h>

// Call once from setup(), after eddiBegin().
void eddiScheduleBegin();

// Call every loop() from the idle state, with current local time.
//
// Acts only when it observes the clock cross a scheduled time while
// running. A scheduled time that passed while the device was off is NOT
// caught up: the device cannot tell "missed because powered off" from
// "the owner changed the mode deliberately", and silently undoing a
// deliberate choice is worse than missing one cycle.
void eddiSchedulePump(const struct tm &nowLocal);

#endif
```

- [ ] **Step 2: Track the previous minute, and fire only on a crossing**

```cpp
static int lastMinuteOfDay = -1; // -1 until the first pump after boot

void eddiSchedulePump(const struct tm &nowLocal) {
  int nowMin = nowLocal.tm_hour * 60 + nowLocal.tm_min;

  // The first pump after boot only records where the clock is. Without
  // this, a device booting at 12:00 would treat the current minute as a
  // crossing and issue a command it never observed happening.
  if (lastMinuteOfDay < 0) { lastMinuteOfDay = nowMin; return; }
  if (nowMin == lastMinuteOfDay) return;

  int stopMin = wifiSetupEddiStopMin();
  int startMin = wifiSetupEddiStartMin();
  bool crossedStop = crossed(lastMinuteOfDay, nowMin, stopMin);
  bool crossedStart = crossed(lastMinuteOfDay, nowMin, startMin);
  lastMinuteOfDay = nowMin;

  if (crossedStop) beginCommand(false);
  else if (crossedStart) beginCommand(true);

  pumpVerify(nowLocal);
}
```

- [ ] **Step 3: Write the crossing test**

```cpp
// True if target lies in (prev, now]. Handles the midnight wrap, where
// now is numerically smaller than prev.
static bool crossed(int prev, int now, int target) {
  if (prev == now) return false;
  if (prev < now) return target > prev && target <= now;
  return target > prev || target <= now; // wrapped past midnight
}
```

- [ ] **Step 4: Write the verify cycle**

```cpp
static const int VERIFY_ATTEMPTS = 5;
static const unsigned long VERIFY_WAIT_MS = 120000; // 2 minutes

// Watches the status the eddi module is already polling. Nothing here
// fetches: it reads eddiStatus() and re-issues if the device has not
// moved, so verification adds no API traffic of its own.
static void pumpVerify(const struct tm &nowLocal) {
  if (!verifyActive) return;
  if (millis() - lastAttemptMs < VERIFY_WAIT_MS) return;

  int sta = eddiStatus();
  bool isStopped = (sta == 6);
  bool reached = wantStart ? (sta != 6 && sta != EDDI_UNKNOWN) : isStopped;

  if (reached) {
    verifyActive = false;
    notifySend(String("Eddi: ") + (wantStart ? "started" : "stopped") +
               " and confirmed at " + hhMm(nowLocal));
    return;
  }

  if (++attempt > VERIFY_ATTEMPTS) {
    verifyActive = false;
    notifySend(String("Eddi: ") + (wantStart ? "start" : "stop") +
               " NOT confirmed after " + String(VERIFY_ATTEMPTS) +
               " attempts. Check the app.");
    return;
  }

  lastAttemptMs = millis();
  eddiSendMode(wantStart);
}
```

- [ ] **Step 5: Wire into the sketch**

`#include "eddi_schedule.h"`, `eddiScheduleBegin()` after `eddiBegin()`, and `eddiSchedulePump(nowLocal)` beside `eddiPump()` in the idle branch.

- [ ] **Step 6: Compile and flash**

- [ ] **Step 7: Verify with a temporary near-future time**

Set the stop time via the portal to two minutes ahead, watch the command fire, the pill turn grey, and a confirmation message arrive. Then set it back to 12:00. This exercises the whole path without waiting for noon.

- [ ] **Step 8: Commit**

```bash
git add eddi_schedule.cpp eddi_schedule.h water-reminder.ino
git commit -m "Add the Eddi scheduler with verified mode changes"
```

---

### Task 4: Documentation and retiring the Pi

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Document the scheduler**

Cover the default times, where to change them, that a missed command after a reboot is deliberately not caught up and why, and the three Telegram outcomes.

- [ ] **Step 2: Replace the "only observes" paragraph**

README line 115 currently states the device does not start or stop the Eddi. That becomes false with this task.

- [ ] **Step 3: Run the full non-ASCII scan over tracked files**

- [ ] **Step 4: Run both schedules in parallel for a few days**

Leave the Pi's cron running alongside the device. Two messages per event, from two independent systems, is the cheapest possible confirmation that the device agrees with the Pi before the Pi is switched off.

- [ ] **Step 5: Only then, disable the Pi's cron**

Comment out the two cron lines rather than deleting them, so reverting is one edit.

- [ ] **Step 6: Commit**

```bash
git add README.md
git commit -m "Document the Eddi scheduler"
```
