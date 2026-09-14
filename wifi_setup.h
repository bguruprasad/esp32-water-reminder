#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include <Arduino.h>

// Tries to connect using credentials saved in NVS (namespace "wifi").
// Returns true if connected within connectTimeoutMs, false if no saved
// credentials or the connection attempt failed/timed out.
bool wifiSetupConnect(unsigned long connectTimeoutMs = 15000);

// Starts a "WaterReminder-Setup" access point + captive portal, serves a
// credential form, saves submitted credentials to NVS, then reboots.
// Blocks forever (never returns) — call only when wifiSetupConnect()
// returned false.
void wifiSetupStartPortal();

#endif
