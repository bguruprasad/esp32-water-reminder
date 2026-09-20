# esp32-water-reminder

A desk companion for the Freenove 2.8" ESP32 touch display (FNK0114B,
ST7789). It shows a full-screen reminder to drink water every 30 minutes,
Monday-Friday, 09:00-18:00 (Europe/Dublin time, DST-aware) - tap the
screen to dismiss, or leave it and it clears itself after 30 seconds.

The screen stands upright in portrait, with the clock at the top and the
lower two thirds carrying price charts for eight US tech stocks, two at
a time.

WiFi credentials and the price API key are entered on the device itself,
through a setup page it serves over its own access point. Nothing secret
lives in this repository.

See [docs/superpowers/specs/2026-09-14-water-reminder-design.md](docs/superpowers/specs/2026-09-14-water-reminder-design.md)
for the full design.

## Hardware

- ESP32 dev board + Freenove 2.8" ST7789 touch TFT (FNK0114B)
- USB connection via CH340 serial (macOS: `/dev/cu.usbserial-1120`; the
  trailing number changes between replugs, so check `ls /dev/cu.usb*`)

## First-time setup

1. Power on the device. If it has no saved WiFi credentials, it starts an
   access point named `WaterReminder-Setup`.
2. From a phone or laptop, join that WiFi network.
3. A setup page should open automatically (or visit `http://192.168.4.1/`).
4. Enter your home WiFi SSID and password, submit.
5. The device reboots, connects to your WiFi, syncs time via NTP, and
   starts running the schedule.

No WiFi credentials are ever stored in this repository.

## Price charts

The lower two thirds of the screen show two stocks at a time from AAPL,
MSFT, GOOGL, AMZN, NVDA, META, TSLA and NFLX, advancing every 5 seconds
so the full set comes round every 20. Each panel carries the symbol and
percent change on one line, the price below it, and a line chart of the
day's trading with a dashed line at the previous close, so points above
that line are up on the day.

Prices and charts are both shown at all hours. Yahoo keeps serving the
most recent session and Finnhub answers with the last close, so outside
market hours (09:30-16:00 ET, Mon-Fri) a panel shows the previous
session's chart alongside that session's closing price. The percent
change is that session's move, so it stops changing once the market
shuts and resumes when it reopens.

Two data sources, deliberately separate:

- **Prices** come from Finnhub and need a free API key from
  https://finnhub.io/register. Enter it in the third field of the
  device's setup page, alongside your WiFi details. The key is stored on
  the device and never appears in this repository. The field is optional:
  left blank, no prices are fetched. One symbol is refreshed every 12
  seconds, so each of the eight updates about every 96 seconds, which is
  5 requests a minute and comfortably inside the free rate limit.
- **Charts** come from Yahoo Finance and need no key at all. One symbol
  is fetched every 30 seconds, so the eight refresh about every four
  minutes. This is an undocumented endpoint with no contract behind it,
  so it may change or stop working without notice. If it does, the panels
  keep showing prices and the rest of the device is unaffected.

Line charts rather than candlesticks: at 58px of panel height and 240px
of width, a candle body would render about 5px wide with 1px wicks, which
does not read on this panel.

## Eddi status

A row between the clock and the first chart panel shows the state of a
myenergi Eddi water heater: a pill reading "Eddi" with a drawn symbol on
the left, the weekday on the right, both in the same colour.

| State | Symbol | Colour | Meaning |
|-------|--------|--------|---------|
| Diverting | play | green | actively heating from surplus |
| Boosting | play | green | heating on demand |
| Paused | two bars | amber | on, waiting for surplus |
| Max Temp | two bars | amber | on, tank already hot |
| Stopped | square | grey | off |
| Unknown | hollow square | dim grey | no reading yet, or unreachable |

When the status changes, a Telegram message names the transition and the
time, for example `Eddi: Stopped -> Diverting at 22:04`.

Two cases deliberately send nothing. The first reading after boot has
nothing to compare against, so without this every power cut would
announce itself. And a change into or out of the unknown state means
contact was lost or regained, not that the heater did anything, so a
WiFi blip does not produce a pair of false alerts.

Credentials go in the device's setup page, never in this repository: the
myenergi hub serial (the HTTP Digest username), the myenergi API key
(its password), and optionally a Telegram bot token and chat ID. Leaving
the myenergi fields blank disables the pill; leaving the Telegram fields
blank shows the pill but sends nothing.

Two caveats worth knowing:

- The myenergi API is **unofficial** and may change or stop working
  without notice, the same risk the Yahoo chart endpoint carries. A
  failure leaves the previous status on screen rather than blanking it.
- The pill reflects **cloud state**, which can lag the physical device by
  3 to 5 minutes. A command accepted by the cloud is not the same as a
  heater that has acted on it.

The device polls every 30 seconds. Community guidance for this API is at
most one request per 10 seconds, with a stated risk of a temporary IP
ban, so this sits well inside it.

This device only observes. It does not start, stop or boost the Eddi;
that stays with the Raspberry Pi controller at
`github.com/bguruprasad/myenergi-eddi-python-controller`.

## Reaching the setup page

The page appears automatically when there are no saved WiFi credentials.
To reach it on a device that is already connected, touch the screen
during the 8 second countdown shown at boot.

Stored values are prefilled, except the password and API keys: the page
is served over an open access point, so sending those to the browser
would broadcast them to anyone in range. Those fields show whether a
value is saved, and leaving one blank keeps what is already stored.

## Building and flashing

Requires `arduino-cli` with the `esp32:esp32` core (3.3.11) installed, and
the `TFT_eSPI` (configured for FNK0114B) and `TFT_Touch` libraries in the
Arduino sketchbook.

The device runs the `no_fs` partition scheme (2MB app slot, twice, with
OTA kept). This is not the board default, so it must be passed on every
build and upload - a default-scheme flash writes a different partition
table and erases the saved WiFi credentials.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=no_fs .
arduino-cli upload -p /dev/cu.usbserial-1120 --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=no_fs,UploadSpeed=115200 .
```

## Debug mode

`config.h` has a `DEBUG_FAST_SCHEDULE` toggle that compresses the 30-minute
reminder interval into 30 seconds, with no day/hour restriction, for fast
end-to-end testing. Keep this commented out for normal use.

## Non-goals / not yet built

- No audible beep - no speaker/buzzer hardware attached yet.
- No persistent history of dismissed reminders.
- No remote control or mobile app.
