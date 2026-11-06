# HoloPet: Tamagotchi on ESP32 HoloClock Hardware

## Context

The user has a **spare HoloClock board** (same ESP32-PICO-D4 hardware) and wants to build a **dedicated tamagotchi-style virtual pet device** as a gift. The existing HoloClock firmware will be used as a framework — keeping the driver layer (display, IMU, RGB, ambient light, SPIFFS) but stripping out clock/watcher/video code and replacing it with a pet game engine.

**Key decisions:**
- **Pet style**: Classic retro pixel art tamagotchi
- **Display**: Full-screen pet — no clock, dedicated pet device
- **Controls**: IMU gestures only (no buttons)
- **Battery**: LiPo + TP4056 with easy soldering (USB-C pad taps for VDD_5V/GND). Battery monitoring via QFN soldering is a stretch goal after practicing.
- **Optics**: No beam splitter — TFT viewed directly, display rotation `0` (not mirrored `4`)
- **Case**: Custom 3D printed enclosure (future task)

---

## Architecture Overview

### What to keep from HoloClock firmware
- `driver/display` — ST7789 + LVGL initialization
- `driver/imu` — MPU6050 gesture detection + raw accel
- `driver/rgb_led` — WS2812 status LEDs
- `driver/ambient` — BH1750 light sensor (day/night cycle)
- `driver/flash_fs` — SPIFFS for save data
- `common.h` — pin definitions, shared types
- LVGL library (v8.3)
- WiFi stack (for future features: pet visiting, firmware updates)

### What to strip out
- `app/watcher/` — clock face, NTP sync, time display, now-playing marquee
- `app/picture/` — MJPEG video playback
- HTTP file management server (port 81 endpoints)
- WiFi config portal (port 80)
- SD card dependency (not needed for pet)

### New code to create

```
src/
├── Holo.cpp              (EDIT — simplified setup/loop with pet game)
├── common.h              (EDIT — keep pins, remove unused)
├── pet/
│   ├── pet.h             (pet state struct, lifecycle API)
│   ├── pet.cpp           (pet logic: stats decay, aging, state transitions)
│   ├── pet_ui.h          (UI rendering API)
│   ├── pet_ui.cpp        (LVGL sprites, animations, screens)
│   ├── pet_actions.h     (gesture → action mapping)
│   ├── pet_actions.cpp   (interpret IMU gestures as pet interactions)
│   ├── pet_save.h        (persistence API)
│   └── pet_save.cpp      (save/load pet state to SPIFFS)
├── sprites/
│   └── (pixel art sprite headers, converted to C arrays)
└── driver/               (keep existing, untouched)
```

---

## Game Design

### Pet Stats

| Stat | Range | Decay Rate | How to Restore |
|------|-------|-----------|----------------|
| **Hunger** | 0–100 | -1 every 10 min | Shake to feed |
| **Happiness** | 0–100 | -1 every 15 min | Tilt left/right to play |
| **Energy** | 0–100 | -1 every 20 min | Set device down (stillness) to let pet sleep |
| **Age** | 0–∞ | +1 every hour | Cannot be changed |

### Pet Mood (derived from stats)

| Mood | Condition | Visual |
|------|-----------|--------|
| **Happy** | hunger > 60 AND happiness > 60 | Bouncing, smiling |
| **Neutral** | hunger 30–60 OR happiness 30–60 | Standing, blinking |
| **Hungry** | hunger < 30 | Drooling, stomach growl icon |
| **Sad** | happiness < 30 | Droopy, rain cloud |
| **Sleepy** | energy < 30 | Yawning, half-closed eyes |
| **Sleeping** | energy < 15 OR ambient light low | Zzz animation |
| **Sick** | hunger < 10 AND happiness < 10 | Pale, thermometer icon |
| **Dead** | hunger == 0 for 2+ hours | X eyes, game over screen |

### IMU Gesture Mapping

| Gesture | Action | Game Effect |
|---------|--------|-------------|
| **Shake** | Feed | hunger += 20 (max 100) |
| **Tilt left** | Pet/play left | happiness += 10, pet walks left |
| **Tilt right** | Pet/play right | happiness += 10, pet walks right |
| **Tilt forward (moderate)** | Discipline | Stops bad behavior |
| **Sustained stillness** | Let pet rest | energy recovery (+2/min) |
| **Pick up from rest** | Wake pet | Pet wakes with animation |
| **Flip upside down** | Pet reacts | Funny animation, slight happiness boost |

### Ambient Light Integration

- **Dark room**: Pet auto-sleeps, energy recovers faster, screen dims
- **Bright room**: Pet is active, normal stat decay
- **Transition**: Wake/sleep animations when light changes

### RGB LED Indicators

| Color | Meaning |
|-------|---------|
| Green pulse | Pet is happy and healthy |
| Yellow pulse | Pet needs attention (hunger or happiness low) |
| Red pulse | Pet is sick or starving |
| Blue steady | Pet is sleeping |
| Rainbow | Pet just evolved or special event |

### Pet Lifecycle / Evolution (stretch goal)

| Stage | Age | Sprite |
|-------|-----|--------|
| **Egg** | 0–1 hr | Wobbling egg |
| **Baby** | 1–24 hr | Small simple creature |
| **Child** | 1–3 days | Slightly larger, more detail |
| **Adult** | 3+ days | Full creature, personality variant based on care quality |

---

## Persistence (Save/Load)

Save pet state to SPIFFS as a small JSON file (`/pet_save.json`):
```json
{
  "hunger": 75,
  "happiness": 60,
  "energy": 90,
  "age_minutes": 1440,
  "last_save_epoch": 1711036800,
  "alive": true,
  "stage": 2
}
```

- **Auto-save** every 5 minutes and on sleep
- **On boot**, load save and calculate stat decay for elapsed time since last save (so stats drop realistically even when powered off)
- **Reset**: Hold device upside down + shake for 5 seconds → new egg

---

## Main Loop Structure

```
setup():
    init display, IMU, RGB, ambient, SPIFFS
    load pet save (or create new egg)
    create LVGL pet UI
    start IMU poll timer (200ms)

loop():
    if sleeping:
        poll IMU for wake → exitSleep()
        return

    handle IMU gestures → pet_actions
    pet_update(elapsed_ms)     // stat decay, mood transitions
    pet_ui_update()            // animate sprite, update indicators
    screen.routine()           // LVGL task handler
    auto_save_check()
    ambient_light_check()      // day/night transitions
```

---

## Battery Approach

### Phase 1: Easy soldering (recommended starting point)

**Parts:** LiPo cell (3.7V, 500–1000mAh) + TP4056 module

**Solder points (large, accessible pads):**
1. **VDD_5V**: Tap from USB-C connector VBUS pin (large pad on MainBoard)
2. **GND**: Tap from USB-C connector ground (large pad)
3. Wire TP4056 IN+ to VBUS, OUT+ to VDD_5V rail, GND throughout
4. LiPo connects to TP4056 BAT+/BAT-

**ME6211C33 on ExtendBoard** accepts 2.0–6.0V, so LiPo's 3.0–4.2V feeds directly into the VDD_5V rail with no issues.

**No QFN soldering needed.** No battery monitoring — just a simple power source.

### Phase 2: Stretch goal (after practicing fine-pitch soldering)

Add battery voltage monitoring:
- Bodge wire from ESP32 GPIO 37 QFN pad → voltage divider → BAT+
- Enable `#define PEAK` in firmware
- Display battery level icon on pet screen

---

## Sprite Art Approach

LVGL supports image widgets with indexed color PNG → C array conversion. Use LVGL's online image converter or `lv_img_conv` tool:

1. Draw pixel art sprites at 32x32 or 48x48 pixels (small enough for memory, large enough to see on 240x240)
2. Convert to C header arrays with LVGL image converter
3. Store in `sprites/` directory as header files
4. Use `lv_img_set_src()` to swap sprites for animations

Animation: cycle through sprite frames on a timer (e.g., 4-frame walk cycle at 200ms per frame).

---

## Implementation Order

1. **Strip firmware** — remove clock/video/HTTP, keep drivers, verify clean build
2. **Pet state engine** — stats, decay, mood calculation, save/load
3. **IMU action mapping** — gesture → pet action translation
4. **Basic UI** — single placeholder sprite, mood text, stat bars
5. **Sprite art** — draw actual pixel pet sprites, idle/eat/play/sleep animations
6. **Ambient light + RGB** — day/night cycle, LED mood indicators
7. **Evolution system** — egg → baby → child → adult stages
8. **Battery mod** — hardware: LiPo + TP4056 wiring
9. **3D printed case** — design enclosure for board + battery + display

---

## Critical Files

| File | Action |
|------|--------|
| `Holo-fw/src/Holo.cpp` | EDIT — strip to minimal setup/loop with pet game |
| `Holo-fw/src/common.h` | EDIT — keep pin defs, remove unused |
| `Holo-fw/src/pet/pet.h` | CREATE — pet state struct and API |
| `Holo-fw/src/pet/pet.cpp` | CREATE — game logic |
| `Holo-fw/src/pet/pet_ui.h` | CREATE — UI API |
| `Holo-fw/src/pet/pet_ui.cpp` | CREATE — LVGL rendering |
| `Holo-fw/src/pet/pet_actions.h` | CREATE — gesture mapping API |
| `Holo-fw/src/pet/pet_actions.cpp` | CREATE — IMU → action translation |
| `Holo-fw/src/pet/pet_save.h` | CREATE — persistence API |
| `Holo-fw/src/pet/pet_save.cpp` | CREATE — SPIFFS save/load |
| `Holo-fw/src/sprites/*.h` | CREATE — pixel art C arrays |

---

## Verification

1. **Build**: `cd Holo-fw && pio run` after each implementation step
2. **Upload + monitor**: Flash to spare board, verify serial output
3. **Gesture test**: Confirm all IMU gestures trigger correct pet actions
4. **Persistence test**: Power cycle, verify pet state survives reboot
5. **Stat decay test**: Leave running, verify hunger/happiness decrease on schedule
6. **Ambient test**: Cover sensor → pet sleeps; uncover → pet wakes
7. **RGB test**: Verify LED colors match pet mood
