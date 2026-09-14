#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <time.h>

// True if nowLocal falls inside the active reminder window.
// Normal mode: Monday-Friday, 09:00-18:00 (exclusive of 18:00 itself).
// Debug mode (DEBUG_FAST_SCHEDULE): always true (no day/hour restriction).
bool scheduleIsWithinWindow(const struct tm &nowLocal);

// Returns the next scheduled reminder mark strictly after lastFiredMark,
// on the same calendar day as nowLocal. Normal mode: 30-minute marks
// (09:00, 09:30, ..., 17:30). Debug mode: 30-second marks.
struct tm scheduleNextMark(const struct tm &nowLocal, const struct tm &lastFiredMark);

// True if nowLocal has reached/passed the next mark after lastFiredMark
// and nowLocal is still within the active window.
bool scheduleIsMarkDue(const struct tm &nowLocal, const struct tm &lastFiredMark);

// Floors t down to the most recent scheduled mark boundary: the top of
// the hour or half-hour in normal mode (e.g. 09:07:33 -> 09:00:00,
// 09:41:02 -> 09:30:00), or the nearest 30-second boundary in debug mode.
// Used to grid-align lastFiredMark so the schedule always lands on
// 09:00/09:30/... rather than drifting from whatever time it was seeded.
struct tm scheduleFloorToMark(const struct tm &t);

#endif
