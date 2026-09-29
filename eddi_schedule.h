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
// caught up.
//
// The water reminder does the opposite: it floors to the most recent
// mark on boot so a missed reminder still fires. That is right for a
// prompt nobody acted on and wrong here. The device sees only the
// current mode, never the reason for it, so "this did not happen
// because I was powered off" and "the owner started it from the app an
// hour ago" look identical. Catching up would silently undo a
// deliberate choice with nothing on screen to explain why. Missing one
// cycle costs a tap in the app; the alternative costs trust.
void eddiSchedulePump(const struct tm &nowLocal);

#endif
