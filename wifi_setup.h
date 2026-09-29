#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include <Arduino.h>

// Tries to connect using credentials saved in NVS (namespace "wifi").
// Returns true if connected within connectTimeoutMs, false if no saved
// credentials or the connection attempt failed/timed out.
bool wifiSetupConnect(unsigned long connectTimeoutMs = 15000);

// Starts a "WaterReminder-Setup" access point + captive portal, serves a
// credential form, saves submitted credentials to NVS, then reboots.
// Blocks forever (never returns) - call only when wifiSetupConnect()
// returned false.
void wifiSetupStartPortal();

// Returns the Finnhub API key saved via the setup portal, or "" if none
// has been entered. The key lives only in NVS - never in source control.
String wifiSetupApiKey();

// The myenergi credentials, for the Eddi status pill. The hub serial is
// the HTTP Digest username and the API key its password. Both return ""
// when unset, which disables Eddi polling entirely.
String wifiSetupEddiSerial();
String wifiSetupEddiKey();

// Telegram bot credentials, for status change alerts. Both must be set
// for a message to be sent; either one empty disables notifications and
// leaves the rest of the device unaffected.
String wifiSetupTgToken();
String wifiSetupTgChat();

// The Eddi schedule, as minutes since midnight in local time (0-1439).
// Default to 12:00 and 22:00 when nothing is stored, so the device has a
// usable schedule before anyone visits the setup page.
int wifiSetupEddiStopMin();
int wifiSetupEddiStartMin();

// Which chart the price panels draw.
//   0 alternate, a full pass of day lines then a full pass of candles
//   1 candles only, the last 30 minutes at 1 minute resolution
//   2 line only, the whole session at 15 minute resolution
// Defaults to 1. Anything unrecognised in storage also reads as 1, so a
// device that has never seen the setup page shows candles.
int wifiSetupChartMode();

#endif
