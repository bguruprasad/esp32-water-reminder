# Water Reminder

An ESP32 + Freenove 2.8" touch TFT (FNK0114B, ST7789) device that shows a
full-screen reminder to drink water every 30 minutes, Monday-Friday,
09:00-18:00 (Europe/Dublin time, DST-aware). Tap the screen to dismiss.

See [docs/superpowers/specs/2026-09-14-water-reminder-design.md](docs/superpowers/specs/2026-09-14-water-reminder-design.md)
for the full design.

## Hardware

- ESP32 dev board + Freenove 2.8" ST7789 touch TFT (FNK0114B)
- USB connection via CH340 serial (macOS: `/dev/cu.usbserial-120`)

## First-time setup

1. Power on the device. If it has no saved WiFi credentials, it starts an
   access point named `WaterReminder-Setup`.
2. From a phone or laptop, join that WiFi network.
3. A setup page should open automatically (or visit `http://192.168.4.1/`).
4. Enter your home WiFi SSID and password, submit.
5. The device reboots, connects to your WiFi, syncs time via NTP, and
   starts running the schedule.

No WiFi credentials are ever stored in this repository.

## Price ticker

The bottom of the idle screen shows a share-price band covering AAPL,
MSFT, GOOGL, AMZN, NVDA, META, TSLA and NFLX. Two symbols are shown at a
time in bold, swapping to the next pair every 5 seconds, so the full set
comes round every 20 seconds. The band is hidden outside US market hours
(09:30-16:00 ET, Mon-Fri).

It needs a free Finnhub API key from https://finnhub.io/register. Enter
it in the third field of the device's setup page, alongside your WiFi
details. The key is stored on the device and never appears in this
repository. The field is optional: left blank, no prices are fetched and
the band reads "Ticker: no API key" during market hours.

Prices refresh one symbol every 12 seconds in rotation, so each of the
eight is updated about every 96 seconds. That is 5 requests a minute,
comfortably inside Finnhub's free rate limit — the interval sets the
request rate, so adding symbols lengthens the refresh cycle rather than
using more of the quota.

## Building and flashing

Requires `arduino-cli` with the `esp32:esp32` core (3.3.11) installed, and
the `TFT_eSPI` (configured for FNK0114B) and `TFT_Touch` libraries in the
Arduino sketchbook.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload -p /dev/cu.usbserial-120 --fqbn esp32:esp32:esp32 \
  --board-options UploadSpeed=115200 .
```

## Debug mode

`config.h` has a `DEBUG_FAST_SCHEDULE` toggle that compresses the 30-minute
reminder interval into 30 seconds, with no day/hour restriction, for fast
end-to-end testing. Keep this commented out for normal use.

## Non-goals / not yet built

- No audible beep — no speaker/buzzer hardware attached yet.
- No persistent history of dismissed reminders.
- No remote control or mobile app.
