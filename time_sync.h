#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>

// Starts NTP sync using the configured timezone. Blocks up to a few
// seconds waiting for the first successful sync. Returns true if the
// clock now reads a sane date (year > 2020), false if sync hasn't
// landed yet (caller should keep polling timeSyncIsValid()).
bool timeSyncStart();

// Cheap check: does the system clock currently hold a sane, synced time?
bool timeSyncIsValid();

#endif
