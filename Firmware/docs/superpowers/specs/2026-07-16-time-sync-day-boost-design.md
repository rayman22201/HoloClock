# Time-Sync Fix + Adaptive Day-Boost — Design

**Date:** 2026-07-16
**Status:** Approved (interactive review) + hardened per adversarial review (3 independent reviewers, verdict SOUND-WITH-FIXES)

## Problem

1. **Clock drift:** The clock falls ~10 minutes behind after a day or so of uptime and
   only recovers on restart.
2. **Daytime readability:** Text is hard to read through the Pepper's ghost beamsplitter
   in bright ambient light.

## Root Cause (drift)

`watcher.cpp` already has a "15-minute sync," but it is broken by design:

- The render loop calls `UpdateTime_RTC()` every ~800ms, which writes a
  `millis()`-extrapolated, seconds-truncated value into the system clock via
  `ESP32Time::setTime()` → `settimeofday()`.
- The background SNTP client (started by `configTime()` at boot, hourly re-poll,
  immediate-step — verified in arduino-esp32 1.0.6) steps the clock correctly — and is
  overwritten within 800ms.
- The "15-minute sync" (`getWorldTime()`) does not query NTP; it reads `time(nullptr)`,
  which the loop itself just wrote — re-anchoring to its own value.

**Dominant drift mechanism:** after the first boot sync, the clock free-runs on the
crystal/`millis()` timebase for the entire uptime with every SNTP correction discarded.
Seconds-truncation at the 15-min re-anchor adds a bounded, always-negative ~1 min/day
on top. Together: monotonic slow drift, fixed only by reboot.

## Design — Part 1: system clock as single source of truth

All in `Holo-fw/src/app/watcher/watcher.cpp` (EN_TIME_ZONE path):

1. `UpdateTime_RTC(long long ms)` becomes a pure formatter: `localtime_r()` → `TimeStr`
   → `display_time()`. No `settimeofday()`, no `ESP32Time` writes. `g_rtc` removed.
   (Verified: identical TZ semantics to the old `g_rtc` read path in both EN and ZH
   builds; ZH feeds pre-offset timestamps and has TZ unset, same as today.)
2. Render tick reads `time(nullptr)` once per second when valid (> 1700000000), even if
   WiFi has dropped (the system clock keeps ticking on the crystal).
3. **Hourly forced re-sync (non-blocking):** every 60 min with WiFi connected, re-run
   `configTime(daylightOffset_sec, 0, ntpServer)` using the cached offset. This restarts
   SNTP and triggers an immediate NTP query — guaranteeing at-least-hourly stepping by
   construction, independent of whether lwIP's background poll stays alive. Worst-case
   steady-state drift = crystal error over 1h (~40–150ms at 10–40ppm).
4. **Daily timezone/DST refresh (bounded blocking):** every 24h with WiFi connected,
   re-query ip-api.com then `configTime()`. The refresh path uses
   `http.setTimeout(2000)` and max 2 attempts (≤ ~4s worst-case render-loop stall, once
   per day) — NOT the boot-path 5-retry/no-timeout `getTimezone()`.
5. Offline-since-boot fallback: keep millis-extrapolation from `preNetTimestamp` for
   display only. Known cosmetic limitation (pre-existing): with no WiFi ever, TZ is
   unset and the seed epoch is March 2023 — display ticks but is wrong until first sync.
6. `ZH_TIME_ZONE` path (not compiled by default) keeps its HTTP-poll + extrapolation
   behavior, routed through the shared formatter (semantics unchanged, see #1).
7. `/time-debug` JSON gains `day_boost`, `day_boost_mode`, and `last_resync_ms`
   (millis of last forced configTime) for on-device verification.

## Design — Part 2: day-boost (time schedule + HTTP override)

**Hardware finding (2026-07-16):** the BH1750 is NOT populated on this unit — an I2C
bus scan (`/i2c-debug` diagnostic endpoint, kept in the firmware) shows only the
MPU6050 (0x68) ACKing; both BH1750 addresses (0x23/0x5C) NACK. The original
lux-adaptive design was replaced with a local-time schedule + HTTP override
(user decision).

1. `brightness_poll()` runs every 2s from `update_time_status()` (skipped during soft
   sleep automatically; loop returns early).
2. **Trigger:** day mode is active 07:00–20:00 local time (`DAY_START_HOUR` /
   `DAY_END_HOUR` in `watcher.cpp`), only once the clock is valid — never-synced
   devices keep the night look. `/day-boost?v=on|off|auto` (port 81) overrides the
   schedule in either direction; `auto` returns to it. Mode is reported in
   `/time-debug` as `day_boost_mode`. Override is RAM-only (reverts to auto on reboot).
3. **Driver hardening (`driver/ambient.cpp`, kept although sensor is absent):**
   `Wire.read()` returns -1 on a failed read → truncates to 0xFF → a dead sensor reads
   ~54,600 lux. Fix: only accept a sample when `Wire.requestFrom()` returns 2 bytes and
   raw != 0xFFFF.
4. **Idempotent application:** the poll re-asserts the backlight level for the current
   state on every poll (not only on transitions). This heals `exitSleep()`'s hardcoded
   `setBackLight(0.95)` within one poll (~2s) after a daytime wake, and any future UI
   re-init. Colors are re-applied on transitions (LVGL label objects persist for the
   life of the process).
5. **Day mode:** backlight 100%; month `0xE2B483` → `0xFFD9A6`, date `0xF5D8A8` →
   `0xFFEBC8` (same warm hue, ~95% luminance). Time/AM-PM/ticker already pure white.
6. **Night mode:** exactly the current look (95% backlight, current tan colors).
7. Star-field background image: full opacity at night (today's look), dimmed to ~40%
   (`bg_img_opa` ≈ 100/255) while day boost is active — revised user decision after
   review flagged that the backlight step alone is a weak lever (~5%) for the
   already-white time digits, and a brighter background works against text contrast
   through the beamsplitter.
8. New `ui_set_day_boost(bool)` in `ui.cpp`/`ui.h`; day color defines in
   `ui_img_1_png.h` next to the existing theme constants.

## Files touched

- `Holo-fw/src/app/watcher/watcher.cpp` — time architecture, resync scheduling,
  day-boost state machine, debug JSON
- `Holo-fw/src/app/watcher/ui.cpp`, `ui.h` — `ui_set_day_boost()`
- `Holo-fw/src/app/watcher/ui_img_1_png.h` — `MOUTH_COLOR_DAY`, `DATE_COLOR_DAY`
- `Holo-fw/src/driver/ambient.cpp` — reject failed I2C reads (Part 2 #2)

## Verification

- `cd Holo-fw && pio run` builds clean.
- On device: `/time-debug` shows `time_valid: true` and `last_resync_ms` advancing
  hourly; compare `time_raw` against wall clock across ≥24h — drift should be < 1s.
- `/day-boost?v=off` then `v=on` then `v=auto` → mode flips within ~2s (visible +
  `day_boost`/`day_boost_mode` in `/time-debug`); during 07:00–20:00 local, `auto`
  keeps boost active. Wake from sleep during day hours → backlight returns to 100%
  within ~2s.
