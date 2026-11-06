#ifndef _NOWPLAYING_H
#define _NOWPLAYING_H

#include "lvgl.h"
#include <Arduino.h>

// --- Ticker (now playing) layout ---
#define TICKER_X  0
#define TICKER_Y  62
#define TICKER_W  220
#define TICKER_H  26
#define TICKER_COLOR 0xFFFFFF

#ifdef __cplusplus
extern "C" {
#endif

void nowplaying_create(lv_obj_t *parent);
void nowplaying_destroy(void);
void nowplaying_poll(void);
void nowplaying_debug_json(String* out);
void nowplaying_parallax_start(int16_t base_y, int16_t amp, uint32_t period_ms);
void nowplaying_parallax_stop(void);

#ifdef __cplusplus
}
#endif

#endif
