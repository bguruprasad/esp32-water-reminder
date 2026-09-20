# Eddi Status Pill and Telegram Alerts - Design Spec

Date: 2026-09-20
Status: Draft (awaiting review)

## Purpose

Show the live status of the myenergi Eddi water heater on the water
reminder device, and send a Telegram message when that status actually
changes, so the owner learns what the Eddi really did without being at
the desk or opening the myenergi app.

This replaces nothing. The existing Raspberry Pi controller
(`github.com/bguruprasad/myenergi-eddi-python-controller`) keeps issuing
the scheduled start/stop commands. In this phase the device only
observes; the phase 2 addendum below moves that schedule onto the
device and retires the Pi.

## Why this is worth building

The Pi announces the commands it *sends*. It cannot reliably announce
what the Eddi *did*, because the myenergi cloud takes 3-5+ minutes to
propagate a command to the physical device, and reports the old status
throughout (recorded in the controller's own notes: the 9pm start
consistently failed verification after ~8 minutes, yet the Eddi had in
fact started, discovered only via the mobile app the following morning).

The ESP32 is powered on continuously and already polls two other
services. Polling one more and reporting observed transitions closes
exactly that gap: a message that says the Eddi started is sent because
the Eddi started, not because a command was accepted.

The 10pm start matters most and is the case least served by a desk
display, since the owner is usually away from the desk at that hour.

## Requirements

- The idle screen shows one row between the clock and the first chart
  panel: an Eddi status pill on the left, the weekday on the right.
- The pill shows the Eddi status as a word, not a boolean. The device's
  `sta` field distinguishes Paused, Diverting, Boosting, Max Temp
  Reached and Stopped, and these mean materially different things.
- Status colour: green for Diverting and Boosting (heating now), amber
  for Paused and Max Temp Reached (on, not heating), grey for Stopped,
  dim grey for unknown or unreachable.
- The weekday text takes the same colour as the pill, so the whole row
  reads as one indicator at a glance.
- A Telegram message is sent when the observed status *changes*, never
  on a poll that confirms the existing state.
- Credentials (hub serial, myenergi API key, Telegram bot token,
  Telegram chat ID) are entered only on the device's setup portal and
  stored in NVS. Nothing secret enters the repository or a transcript.
- Every failure path is a no-op. An unreachable myenergi API, a rejected
  credential, or a failed Telegram send must never block the clock, the
  reminder, or the charts.
- Polling must respect the community rate guidance of at most one
  request per 10 seconds, which carries a stated risk of a temporary IP
  ban.

## Non-goals

- **No control in this phase.** The device does not start, stop or boost
  the Eddi. See the phase 2 addendum below, which supersedes the original
  blanket ban.
- No duplication of the Pi's own notifications. The Pi announces its
  commands; this device announces observed transitions. Running both is
  intended, and they say different things.
- No history, graphing or logging of Eddi state on the device.
- No Zappi, Harvi or Libbi support, though the same API serves them.
- No replacement of the Pi controller. The cron schedule stays.

## Architecture

Two new modules, following the shape of the existing `chart.cpp`: own
their own state, expose a small read-only interface, and no-op on every
failure.

### 1. `eddi.cpp` / `eddi.h` - polling and state

- `eddiBegin()` reads credentials from NVS, called once from `setup()`.
- `eddiPump()` called every loop tick; fetches at most once per
  `EDDI_POLL_INTERVAL_MS` (30000, comfortably inside the 1-per-10s
  guidance).
- `eddiStatus()` returns the last observed status code, or a sentinel
  for never-fetched / unreachable.
- `eddiStatusText()` returns the display word.

**Authentication is HTTP Digest**, which Arduino's `HTTPClient` does not
implement (it offers Basic only). It is hand-rolled against
`MD5Builder.h`, which ships with the ESP32 core at 3.3.11:

1. Unauthenticated GET returns 401 with a `WWW-Authenticate` header
   carrying `realm`, `nonce`, `qop`.
2. Compute `HA1 = MD5(serial:realm:apikey)`,
   `HA2 = MD5("GET":uri)`,
   `response = MD5(HA1:nonce:nc:cnonce:qop:HA2)`.
3. Retry with an `Authorization: Digest ...` header.

**Server discovery** is two-hop: `director.myenergi.net/cgi-jstatus-*`
returns an `X_MYENERGI-asn` header naming the hub's real server. The
discovered value is cached in RAM for the session. This hub is known to
live on `s18.myenergi.net`, which is used as the fallback when discovery
fails, so a director outage does not disable the feature.

Status then comes from `GET /cgi-jstatus-E` on that server, and the
first entry of the returned `eddi` array is used. Parsed with
ArduinoJson, already a dependency.

TLS uses `setInsecure()`, consistent with the existing Finnhub and Yahoo
paths and for the same reason: a pinned CA root would expire and
silently break the feature. The accepted risk is that a man-in-the-middle
on the home network could feed a false heater status to a display.

### 2. `notify.cpp` / `notify.h` - Telegram

- `notifySend(const String &text)` posts to
  `api.telegram.org/bot<token>/sendMessage` with `chat_id` and `text`.
- Returns a bool that callers are free to ignore; a failed send is
  logged to screen state only, never retried in a loop.
- No-ops immediately when the token or chat ID is empty, so the feature
  is entirely optional.

### 3. Transition detection

`eddiPump()` holds the previously observed status. A Telegram message is
sent only when a successful fetch yields a status different from the last
successfully observed one.

Deliberately excluded from triggering a message:

- The first successful fetch after boot. Otherwise every power cut
  announces itself.
- Any transition into or out of the unknown/unreachable sentinel. A
  network blip is not an Eddi event, and treating it as one would
  produce a pair of false messages around every outage.

Message text names the transition and the time, for example
`Eddi: Stopped -> Diverting at 22:04`.

### 4. UI - the status row

The row occupies the gap between the clock block and the first chart
panel. Current portrait geometry leaves y=64 to y=84, which is 20px:
too small for the 26px pill recovered from the landscape build.
`IDLE_CLOCK_Y` moves from 34 to 28, raising the clock block by 6px and
opening the gap to 26px. The chart panels do not move: `PANEL_TOP[1]`
plus `PANEL_H` already equals 320 exactly, so there is no slack below.

Width at 240px, in `FreeSansBold9pt7b` at roughly 10px per character:
the longest status word ("Max Temp Reached", ~160px plus 24px of pill
padding) alongside a full weekday ("Wednesday", ~90px) overflows the
screen. Both are therefore shortened: the status displays as "Max Temp",
and weekdays display as three letters. Worst case becomes roughly
108 + 30 = 138px, leaving comfortable margin.

The row repaints only when its content changes, following the
partial-redraw discipline that removed the screen flicker twice before.

### 5. Setup portal

Four new fields on the existing form, all optional:

- myenergi hub serial (the Digest username)
- myenergi API key (the Digest password)
- Telegram bot token
- Telegram chat ID

NVS keys must be 15 characters or fewer. Any field left blank disables
its feature quietly: no myenergi credentials means the pill shows
unknown, no Telegram credentials means transitions are displayed but not
sent.

## Data flow

```
loop tick
 -> eddiPump()
    -> rate limited to once per 30s
    -> Digest GET status (discovering the server on first use)
       -> success: parse sta
          -> changed from last observed, and not first fetch,
             and neither side is the unknown sentinel?
             -> notifySend("Eddi: <old> -> <new> at HH:MM")
          -> store as last observed
       -> failure: leave last observed untouched, mark unreachable
 -> uiUpdateIdleScreen() repaints the status row only if its text or
    colour changed
```

## Error handling

- No credentials: the pill shows a dim "Eddi: --". Nothing is fetched.
- 401 after a Digest attempt: treated as a bad credential; the pill
  shows unknown. Not retried faster than the normal poll interval, to
  avoid tripping the IP ban the rate guidance warns about.
- Director unreachable: fall back to the known `s18.myenergi.net`.
- Status fetch fails: the previous status stays on screen, matching how
  a failed price or chart fetch already behaves.
- Telegram send fails: dropped. A heater status is not worth a retry
  queue, and a queue that retries on every tick is a way to get a bot
  rate limited.
- A fetch can stall the loop for up to the HTTP timeout (5s, as used by
  the existing fetchers), at most once per 30s. A reminder falling due
  during that window is delayed by at most that bound. This is the same
  tradeoff already accepted for prices and charts.

## Security

- The hub serial is the Digest username and the API key its password.
  Both are entered on the device portal only.
- The hub serial is already present in the public history of the Pi
  controller's repository, via a committed cron log later removed from
  the tree. The serial alone is not a working credential, but the
  myenergi API key should be rotated in the app, and the untracked
  local notes file that also carries the serial should stay untracked.
- A Telegram bot token permits sending as that bot and should be
  treated as a secret of the same class as the others.

## Testing approach

Embedded target, so verification is compile, flash, and observe, in the
same incremental style as the earlier stages:

1. **Digest spike first.** Before any of the above is built, a throwaway
   sketch authenticates against the director and prints the Eddi status
   to the screen. This answers the one genuine unknown - whether a
   hand-rolled Digest handshake succeeds against this server - at the
   cheapest possible cost. If it fails, the design changes fundamentally
   (most likely to the Pi pushing state to the device over HTTP, which
   sidesteps myenergi authentication entirely), and nothing else has
   been built on a false premise.
2. Portal fields: verify all four persist across a reboot, and that
   blank fields disable their feature rather than breaking boot.
3. Pill: verify each status word and colour renders, that the row fits
   at the longest status, and that the clock and charts still sit
   correctly after the `IDLE_CLOCK_Y` change.
4. Transition and Telegram: verify a real transition sends exactly one
   message, that a boot does not, and that pulling WiFi does not.
5. Leave running across a real 10pm start to confirm the message arrives
   when the Eddi actually starts.

## Libraries / dependencies

No new third-party libraries.

- `MD5Builder.h` (ESP32 core 3.3.11, verified present)
- `HTTPClient`, `NetworkClientSecure`, `Preferences` (core, already used)
- `ArduinoJson` 7.4.3 (already a dependency)

## Addendum: phase 2, the device runs the schedule (2026-09-20)

Phase 1 observes. Phase 2 gives the device the Raspberry Pi's job, so
the Pi can be retired and one always-on board does both.

### Correcting the Non-goals

The original spec ruled out control outright, on the grounds that a
mistap on a touchscreen must not be able to stop a water heater. That
reasoning is about **touch**, and it still holds: no tap starts or stops
the Eddi. It does not extend to a **scheduled** action, which has no tap
in it and is the same kind of timer as the water reminder that already
fires at 09:00 unattended. The blanket ban was too broad, and this
addendum replaces it.

### What phase 2 adds

- Two commands, both the same Digest GET already proven in phase 1:
  `/cgi-eddi-mode-E<serial>-1` to start, `-0` to stop.
- A schedule stored in NVS: a stop time and a start time, entered on the
  setup page as 24-hour values, defaulting to the Pi's current 09:00 and
  22:00.
- Verified mode changes. The Python controller issues a command, waits,
  polls, and retries up to five times, because the cloud accepts a
  command that the device can take 3 to 5 minutes to act on. The device
  already polls every 30 seconds for the pill, so verification is nearly
  free here: issue the command, then watch the status it is already
  fetching, and re-issue if the state has not moved after a few minutes.
- A Telegram message on the outcome, distinguishing "commanded" from
  "confirmed" - which is precisely the distinction the Pi cannot make.

### Why the device does this better than the Pi

The controller's own notes record the 21:00 start failing verification
after roughly eight minutes on four separate dates, while the Eddi had
in fact started; it was discovered the next morning through the mobile
app. The Pi stops watching when its cron job exits. A device that is
already polling every 30 seconds does not, so the late transition that
the Pi reports as a failure is one the device would see and report
correctly.

### Open questions for phase 2

- Whether the hard-won 22:00 start time (21:00 and 21:30 both fail
  consistently, per the controller's notes) should be a default or a
  fixed floor.
- What happens when the device reboots between a scheduled stop and the
  next start. The water reminder floors to the most recent mark on boot;
  the same approach would re-issue a missed command rather than skip it,
  but re-issuing a stop that the owner has deliberately overridden from
  the app would be wrong. This needs deciding before implementation.
- Whether a failed command after all retries should keep retrying at the
  poll interval or give up and say so.

## Open items resolved during design

- Status display: the status word, colour-coded, not a boolean.
- Row layout: pill left, weekday right, weekday matching the pill colour.
- Both status and weekday abbreviated, to fit 240px at the worst case.
- Telegram: observed transitions only, not parity with the Pi's
  command announcements, and not omitted.
- Control: out of scope for phase 1, and revisited in the phase 2
  addendum. The original reasoning banned control outright to keep a
  mistap from stopping a water heater; that argument is about touch and
  does not reach a scheduled action.
- Poll interval: 30s, inside the 1-per-10s guidance.
