#ifndef NOTIFY_H
#define NOTIFY_H

#include <Arduino.h>

// Call once from setup(), after WiFi is up.
void notifyBegin();

// True when both a bot token and a chat ID are stored, so callers can
// tell "nothing configured" apart from "configured but the send failed".
bool notifyIsConfigured();

// Posts one message to Telegram. Returns false and does nothing when no
// token or chat ID is configured, or when WiFi is down.
//
// A failed send is dropped, never queued or retried. A heater status is
// not worth a retry queue, and a queue that retries on every tick is a
// way to get a bot rate limited.
bool notifySend(const String &text);

#endif
