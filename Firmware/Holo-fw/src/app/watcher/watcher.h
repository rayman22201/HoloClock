#ifndef _HOLO_WATCHER_H
#define _HOLO_WATCHER_H

#include <Arduino.h>

#define EN_TIME_ZONE
// #define ZH_TIME_ZONE

extern void watch_init();
extern void set_focus_update(int v);
extern void watch_release();
extern void update_time_status();
extern void getTimeDebugJson(String* out);
// -1 = auto (time-of-day schedule), 0 = force night, 1 = force day
extern void set_day_boost_override(int mode);
#endif