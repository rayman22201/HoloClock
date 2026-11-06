# HoloClock firmware: Codex instructions

This is the primary agent guidance for `Firmware/` and its descendants. Start
Codex in this directory (or `Holo-fw/`) so it discovers this file. Paths below
are relative to `Firmware/` unless noted otherwise.

## Working agreements

- Inspect the working tree before editing. Preserve existing changes, including
  deleted legacy picture files and untracked work; do not restore them merely
  because an older document describes them.
- Preserve approved decisions across handoffs. Treat design documents as intent
  and source as implementation evidence; do not claim a design is deployed.
- Keep source comments and commit messages concise. Put investigations and
  validation evidence in `docs/` or the task's existing record.
- Before adding a cache, pool, or retention limit, trace the owning lifecycle
  and compare avoiding allocation, retaining less, and existing cleanup points.
- Report which source/configuration and checks support each claim. A successful
  build does not verify physical display, gestures, networking, or timing.
- Flashing, resetting the device, starting a notification daemon, or changing
  live device settings requires user authorization for that action. A request
  to edit or build firmware alone does not authorize hardware changes.
- Local `tools/config.yaml`, `tools/credentials.json`, and `tools/token.json`
  can contain secrets. Do not print or commit their contents. Use explicit file
  lists when staging; diagnostic logs can also contain private information.

## Build and validation

The PlatformIO project is `Holo-fw/`: Arduino on ESP32 `pico32`, platform
`espressif32 @ ~3.5.0`. Use PlatformIO, not host `cl.exe` or `nmake`, for firmware.
Run these commands from `Firmware/`:

```text
pio project config --project-dir Holo-fw
pio run --project-dir Holo-fw -e Holo_Releases
pio run --project-dir Holo-fw -e Holo_Debug
```

`Holo_Releases` is the default environment; `Holo_Debug` adds `-O0`.
Application libraries are vendored in `Holo-fw/lib/`. PlatformIO may still need
to download its platform, framework, and toolchain. `Libraries/` contains older
or alternate copies and is not the configured project library directory.

For an authorized hardware session, confirm the attached device/port first:

```text
pio device list
pio run --project-dir Holo-fw -e Holo_Releases --target upload --upload-port COM9
pio device monitor --port COM9 --baud 115200
```

The current `platformio.ini` specifies COM9, upload speed 921600, monitor speed
115200. Treat the port as configuration, not proof of which device is connected.
There is no project-owned automated test suite in the current tree. For source
changes, build the affected environments and report any checks not run. Hardware
verification is a separate step. Documentation-only edits need path/configuration
checks rather than flashing.

Use the actual shell reported by the session. In PowerShell, keep commands on
separate lines rather than assuming Bash syntax. In Git Bash use quoted Windows
paths such as `C:/Users/rayim/Downloads/HoloClock/Firmware`, not `/cygdrive/...`.
Use `python`, not `python3`, for host tools.

## Current firmware map

HoloClock uses an ESP32, a 240x240 ST7789 TFT, MPU6050 gestures, WS2812 LEDs,
SD/SPIFFS storage, and WiFi. The original clock/video description is historical:
the current `src/app/picture/picture.cpp` is a thin clock wrapper, and the old
MJPEG decoder and picture GUI files have been removed in this working tree.

All source paths in this section are under `Holo-fw/`:

- `src/Holo.cpp`: hardware setup, main loop, WiFi/gesture handling, HTTP routes,
  and soft sleep/wake. The port 81 server exposes file operations, notification
  input, diagnostics, and device controls. The port 80 captive portal starts
  when WiFi configuration mode is entered, not unconditionally at boot.
  Some GET routes change device state; inspect handlers before invoking them.
- `src/app/picture/picture.cpp`: initializes the watch and updates clock status.
- `src/app/watcher/watcher.cpp`, `watcher.h`: clock, timezone, synchronization,
  and day-boost logic. The default English timezone path uses ip-api.com for
  timezone discovery and NTP; valid system time drives rendering even offline.
  It requests hourly NTP resync and daily timezone refresh. The legacy Chinese
  timezone path differs; inspect it before changing shared code.
- `src/app/watcher/ui.cpp`, `ui.h`: LVGL watch face and notification visuals.
- `src/app/watcher/nowplaying.cpp`, `nowplaying.h`: now-playing integration.
- `src/driver/`: display/LVGL, IMU, RGB LEDs, SD, SPIFFS, ambient sensor,
  and LVGL input/filesystem ports. Driver presence does not prove the sensor
  is populated on the user's device.
- `src/common.h`: pins and display backend (`GFX=0` selects TFT_eSPI).
- `lib/TFT_eSPI/`: vendored display library with local changes; preserve them.

Key hardware settings: I2C SDA=32/SCL=33, WS2812 pin 27, display backlight pin 5.
The clock initializes mirrored rotation 4 for its optics; runtime rotation
controls also exist. Inspect both before changing display orientation.
`partitions-no-ota.csv` allocates one 3 MiB app slot and 960 KiB SPIFFS.

Boot WiFi initialization reads EEPROM. SD `/config.txt` (`ssid:` and
`pass_word:`) is imported through the reconnect/check path. The gesture-triggered
AP portal is separate from the station server. Inspect gesture branches in
`Holo.cpp` and `driver/imu.*` rather than assuming the original tilt-to-switch-video
mapping still applies.

## Host tools and design context

- `tools/holoclock_notify.py`: multi-source notification daemon (Slack, Discord,
  email, calendar, and SMS push input); dependencies in `tools/requirements.txt`.
  Running it connects to services and sends notifications to the device.
- `tools/np_monitor.py`: long-running `/np-debug` poller that appends
  `tools/np_monitor.jsonl`. Run only when a monitoring session is requested.
- `docs/superpowers/specs/2026-07-16-time-sync-day-boost-design.md`: approved
  time-sync and day-boost decisions, including recorded absence of BH1750 on
  one unit. Preserve the schedule/HTTP-override decision. Historical hardware
  observations are not fresh verification of every board; implementation details
  such as HTTP timeouts must be checked against source. Resync requests do not
  guarantee successful synchronization.
- `HOLOPET_DESIGN.md`: separate spare-board virtual-pet design. It is not the
  current clock implementation and does not authorize replacing the clock.

Comments mix Chinese and English. Match surrounding style and preserve meaning.
Runtime structures include manual allocation; check lifetimes and cleanup when
changing them.
