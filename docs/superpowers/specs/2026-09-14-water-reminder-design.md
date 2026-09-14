# Water Reminder — Design Spec

Date: 2026-09-14
Status: Approved

## Purpose

A standalone ESP32 + Freenove 2.8" touch TFT (FNK0114B, ST7789) device that
reminds the user to drink water every 30 minutes during work hours, so they
don't have to remember on their own.

## Requirements

- Reminder fires every 30 minutes, only Monday–Friday, only 09:00–18:00,
  local Dublin time (`Europe/Dublin`, with automatic DST handling — GMT in
  winter, IST/BST-equivalent Irish Summer Time in summer).
- Reminder must be hard to miss: full-screen visual alert. Audible beep is
  deferred (see Non-goals) since no external speaker/buzzer is attached yet.
- Reminder is dismissed by tapping the touchscreen anywhere; dismissing
  returns to the idle screen and resets the timer toward the next 30-minute
  mark.
- No reminders fire outside the configured window (evenings, weekends,
  before 09:00, at/after 18:00).
- Entering the window (e.g. device boots at 9:15) does not immediately fire
  a reminder — the first reminder is the next scheduled mark.
- No WiFi credentials are ever committed to git. The project repo must be
  safe to push publicly.
- The device needs real wall-clock time and correct weekday, since it has
  no battery-backed RTC.

## Non-goals

- No persistent history/analytics of water intake across reboots (a same-day
  in-memory tally is fine as a stretch, not required).
- No mobile app / remote control — this is a self-contained device.
- No support for multiple simultaneous schedules or per-day customization.
- No audible beep for now: the board's only proven audio path
  (Sketch_07_Play_MP3_SD_by_DAC) needs the ESP8266Audio library plus an SD
  card and an I2S DAC output; no external speaker/buzzer is attached to
  the device yet. Revisit once speaker hardware is available — either the
  DAC/I2S path or a simple `ledc` PWM tone, decided then. Full-screen
  tap-to-dismiss is the sole alert channel for this build.

## Architecture

Single Arduino sketch (`water-reminder.ino`), built from four concerns:

### 1. WiFi provisioning

On boot, attempt to connect using credentials stored in NVS flash (via the
`Preferences` library, part of the ESP32 core — no new dependency).

If no credentials are stored, or the stored credentials fail to connect
within a timeout, fall back to a self-hosted captive portal:

- ESP32 starts a WiFi access point (SSID: `WaterReminder-Setup`).
- Built-in `DNSServer` redirects all DNS queries to the device's own IP,
  so any device joining the AP is bounced to a simple HTML form
  (SSID + password fields) automatically (captive-portal behavior).
- Submitting the form saves credentials to NVS and reboots the device,
  which then connects normally.

This uses only libraries already installed in this environment (`WiFi`,
`DNSServer`, `Preferences`, all part of the esp32 Arduino core) — no
WiFiManager or other third-party dependency. No secrets file, no
`.gitignore` entry needed: nothing sensitive ever exists as a file.

### 2. Time sync

Once WiFi is connected, sync time via NTP using `configTzTime()` with the
POSIX TZ string for `Europe/Dublin` (`GMT0IST,M3.5.0/1,M10.5.0`), so DST
transitions are handled automatically by the ESP32's time library rather
than hardcoded.

Re-sync periodically (every few hours) while connected to correct drift.
If WiFi later drops, the device keeps running off its internal clock
(already seeded by the last sync) rather than blocking or failing.

### 3. Schedule engine

Each loop tick, using the current local time:

- Determine if now is Mon–Fri and within 09:00–18:00.
- If outside this window: no reminders; idle screen shows "Outside work
  hours" (or similar) and no countdown.
- If inside this window: compute the next 30-minute mark (09:00, 09:30,
  10:00, ..., 17:30 — 18:00 itself is window close, not a reminder time).
  When the current time reaches/passes that mark and a reminder hasn't
  already fired for it, trigger the alert.
- On boot/window-entry, the "last fired" state is initialized to the most
  recent past mark, so the device waits for the *next* mark rather than
  firing immediately.

### 4. UI

Two screens, using `TFT_eSPI` (configured for FNK0114B) and `TFT_Touch`
exactly as calibrated in the Freenove example sketches:

- **Idle screen**: current time (large), day of week, and either
  "Next reminder: HH:MM" (inside window) or "Outside work hours" (outside
  window).
- **Alert screen**: full-screen message ("💧 Drink Water!" or similar).
  No audio (see Non-goals). Stays until the user taps anywhere on the
  screen, then dismisses back to the idle screen and the schedule engine
  moves on to the next mark.

### Debug mode

A compile-time toggle (`#define DEBUG_FAST_SCHEDULE`) compresses the
30-minute interval down to 30 seconds (and optionally widens/removes the
day/hour window) for fast end-to-end verification during development.
Disabled for real use.

## Data flow

```
boot
 -> try NVS WiFi creds
    -> success: connect, NTP sync -> normal loop
    -> fail/missing: AP + captive portal -> save creds -> reboot

normal loop (every tick):
 read local time (from synced clock)
 -> schedule engine decides: idle vs due-for-reminder
 -> idle screen renders time/countdown, watches for periodic re-render
 -> on reminder due: switch to alert screen, beep, wait for touch
 -> on touch: dismiss, mark this slot as fired, return to idle
```

## Error handling

- WiFi connect timeout during normal boot (not just missing creds) also
  falls back to the captive portal, so a bad password or moved router
  doesn't brick the device into a silent retry loop.
- If NTP sync fails (no internet reachable even though WiFi is connected),
  the device shows a "Time not synced" state on the idle screen rather than
  running the schedule against an unset/garbage clock, and keeps retrying
  sync in the background.
- Touch read glitches (stray/ghost touches) are handled the same way the
  Freenove touch example sketches already debounce — no additional
  handling needed beyond what's proven in Sketch_12.

## Testing approach

Embedded hardware target — no unit test framework. Verification is
compile + flash + on-device behavior check, done incrementally per stage
rather than building the whole thing blind:

1. WiFi captive portal: verify AP appears, form works, credentials persist
   across reboot.
2. Time sync + idle screen: verify correct Dublin local time and weekday
   display.
3. Schedule engine with `DEBUG_FAST_SCHEDULE`: verify reminders fire at the
   compressed interval, respect the (compressed or real) window boundaries.
4. Touch dismiss: verify tap-anywhere clears the alert and resets state.
5. Final: disable debug mode, confirm normal compile/flash, leave running
   to observe one real 30-minute cycle during work hours.

## Libraries / dependencies

All already installed in this environment, no new installs required:

- `TFT_eSPI` (configured for FNK0114B_2P8_240x320_ST7789)
- `TFT_Touch`
- `WiFi`, `DNSServer`, `Preferences` (ESP32 core, built-in)

## Open items resolved during design

- Alert style: full-screen + tap-to-dismiss (audio deferred, no speaker
  hardware yet).
- Time source: WiFi + NTP (`Europe/Dublin`, DST-aware).
- Idle screen: clock + next-reminder countdown.
- WiFi credentials: on-device captive portal, nothing in git.
- Timezone: Dublin.
- Debug mode: yes, compile-time fast-schedule toggle.
