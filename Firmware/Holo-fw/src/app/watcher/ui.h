// SquareLine LVGL GENERATED FILE
// EDITOR VERSION: SquareLine Studio 1.2.1
// LVGL VERSION: 8.3.4
// PROJECT: HoloTimer

#ifndef _HOLOTIMER_UI_H
#define _HOLOTIMER_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

extern lv_obj_t * ui_Screen;
extern lv_obj_t * ui_TH1;
extern lv_obj_t * ui_TH2;
extern lv_obj_t * ui_M1;
extern lv_obj_t * ui_Labelline;
extern lv_obj_t * ui_LabelMouth;
extern lv_obj_t * ui_LabelDate;
extern lv_obj_t * ui_LabelAmPm;

struct TimeStr
{
    int month;
    int day;
    int hour;     // 1-12
    int minute;
    int second;
    int weekday;
    bool is_pm;   // true = PM
};

#define NOTIFY_MAX_SOURCE_LEN 4
#define NOTIFY_DISPLAY_MS 10000
#define NOTIFY_MAX_OPA    230

struct NotificationData {
    char source[NOTIFY_MAX_SOURCE_LEN];  // "S", "E", etc.
    uint32_t color;                       // hex color for dot + LED
    uint32_t duration_ms;                 // auto-dismiss timeout (0 = use NOTIFY_DISPLAY_MS)
    bool pending;
};

extern void ui_change_sec(int);
extern void ui_change_min(int mins);
extern void ui_change_h(int h, bool is_pm);
extern void ui_change_day(int day, int week);
extern void ui_chagne_mouth(int mouth);

extern void ui_init(void);
extern void ui_release();
extern void display_space();
extern void depth_parallax_init(void);
extern void depth_parallax_stop(void);
extern void display_time(struct TimeStr timeInfo, lv_scr_load_anim_t anim_type);

extern void ui_set_day_boost(bool on);

extern void notification_receive(const char* source, uint32_t color, uint32_t duration_ms);
extern void notification_check_display();
extern void notification_dismiss();

extern void float_y_cb(void *obj, int32_t y);
extern void start_float_anim(lv_obj_t *obj, int16_t base_y, int16_t amp, uint32_t period_ms);

extern void ui_show_debug_screen(void);
extern void ui_hide_debug_screen(void);
extern void ui_show_debug_grid(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
