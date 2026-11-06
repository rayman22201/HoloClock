// SquareLine LVGL GENERATED FILE
// EDITOR VERSION: SquareLine Studio 1.2.1
// LVGL VERSION: 8.3.4
// PROJECT: HoloTimer

#include "ui.h"
#include "ui_helpers.h"
#include "Arduino.h"
#include "common.h"
LV_FONT_DECLARE(clock_digits_56);

#define THEME_1
#define SPACE_60


#include "space_image-60.h"
#include "ui_img_1_png.h"


///////////////////// VARIABLES ////////////////////
lv_obj_t * ui_Screen;
lv_obj_t * ui_TH1;
lv_obj_t * ui_TH2;
lv_obj_t * ui_M1;
lv_obj_t * ui_Labelline;
lv_obj_t * ui_LabelMouth;
lv_obj_t * ui_LabelDate;
lv_obj_t * ui_LabelAmPm = NULL;

lv_obj_t *spaceImg = NULL;

const void *manImage_map[] = {&man_0, &man_1, &man_2, &man_3, &man_4, &man_5, &man_6, &man_7, &man_8, &man_9};
static const char weekDayCh[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char mouthCh[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep","Oct", "Nov", "Dec"};

///////////////////// NOTIFICATION STATE ////////////////////
static lv_obj_t * ui_NotifyDot = NULL;
static lv_obj_t * ui_NotifyLabel = NULL;
static lv_timer_t * notify_dismiss_timer = NULL;
static NotificationData g_notification = {{0}, 0, 0, false};
static bool g_notification_visible = false;

// LED breathing state
static lv_timer_t * led_breathe_timer = NULL;
static float led_breathe_phase = 0.0f;
static float led_breathe_step = 0.25f;
static uint8_t led_breathe_r = 0, led_breathe_g = 0, led_breathe_b = 0;

static bool g_parallax_active = false;
static int16_t spaceImg_base_y = 0;

void ui_init(void);
void ui_release();
void ui_change_sec(int);
void ui_change_min(int mins);
void ui_change_h(int h, bool is_pm);
void ui_change_date(int month, int day, int week);
void display_space();
void depth_parallax_init(void);
void depth_parallax_stop(void);

// Forward declarations for notification system
static void notify_opa_anim_cb(void * obj, int32_t v);
static void notify_exit_done_cb(lv_anim_t * anim);
static void led_breathe_cb(lv_timer_t * timer);
///////////////////// DEPTH PARALLAX ////////////////////

void float_y_cb(void *obj, int32_t y) {
    lv_obj_set_y((lv_obj_t*)obj, (lv_coord_t)y);
}

void start_float_anim(lv_obj_t *obj, int16_t base_y, int16_t amp, uint32_t period_ms) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, float_y_cb);
    lv_anim_set_values(&a, base_y - amp, base_y + amp);
    lv_anim_set_time(&a, period_ms / 2);
    lv_anim_set_playback_time(&a, period_ms / 2);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void depth_parallax_init(void) {
    if (g_parallax_active || ui_Screen == NULL) return;

#if DEPTH_INVERT
    #define _FAR_AMP    PARALLAX_NEAR_AMP
    #define _FAR_PER    PARALLAX_NEAR_PERIOD
    #define _NEAR_AMP   PARALLAX_FAR_AMP
    #define _NEAR_PER   PARALLAX_FAR_PERIOD
#else
    #define _FAR_AMP    PARALLAX_FAR_AMP
    #define _FAR_PER    PARALLAX_FAR_PERIOD
    #define _NEAR_AMP   PARALLAX_NEAR_AMP
    #define _NEAR_PER   PARALLAX_NEAR_PERIOD
#endif

    // Use the original alignment offsets, NOT lv_obj_get_y().
    // lv_obj_get_y() returns computed pixel position, but lv_obj_set_y()
    // (in float_y_cb) sets offset from the alignment point (CENTER).
    // Using computed positions as alignment offsets doubles the offset.

    // Far layer: month + date
    start_float_anim(ui_LabelMouth, MOUTH_Y, _FAR_AMP, _FAR_PER);
    start_float_anim(ui_LabelDate,  DATE_Y,  _FAR_AMP, _FAR_PER);

    // Mid layer: HH:MM
    start_float_anim(ui_TH1, TH1_Y, PARALLAX_MID_AMP, PARALLAX_MID_PERIOD);
    start_float_anim(ui_TH2, TH2_Y, PARALLAX_MID_AMP, PARALLAX_MID_PERIOD);

    // Near layer: AM/PM + spaceImg
    start_float_anim(ui_LabelAmPm, AMPM_Y, _NEAR_AMP, _NEAR_PER);
    start_float_anim(spaceImg, spaceImg_base_y, _NEAR_AMP, _NEAR_PER);

    g_parallax_active = true;

#undef _FAR_AMP
#undef _FAR_PER
#undef _NEAR_AMP
#undef _NEAR_PER
}

void depth_parallax_stop(void) {
    if (!g_parallax_active) return;

    // Remove float animations from all depth objects
    lv_anim_del(ui_LabelMouth, float_y_cb);
    lv_anim_del(ui_LabelDate, float_y_cb);
    lv_anim_del(ui_TH1, float_y_cb);
    lv_anim_del(ui_TH2, float_y_cb);
    lv_anim_del(ui_LabelAmPm, float_y_cb);
    if (spaceImg != NULL) lv_anim_del(spaceImg, float_y_cb);

    g_parallax_active = false;
}

///////////////////// ANIMATIONS ////////////////////

///////////////////// FUNCTIONS ////////////////////

///////////////////// SCREENS ////////////////////
void ui_Screen_screen_init(void)
{
    ui_Screen = lv_obj_create(NULL);
    lv_obj_add_flag(ui_Screen, LV_OBJ_FLAG_SCROLL_ONE);     /// Flags
    lv_obj_clear_flag(ui_Screen, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_Screen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(ui_Screen, &ui_img_1_png, LV_PART_MAIN|LV_STATE_DEFAULT);

    ui_TH1 = lv_label_create(ui_Screen);
    lv_obj_set_width(ui_TH1, TH1_W);
    lv_obj_set_height(ui_TH1, TH1_H);
    lv_obj_set_x(ui_TH1, TH1_X);
    lv_obj_set_y(ui_TH1, TH1_Y);
    lv_obj_set_align(ui_TH1, LV_ALIGN_CENTER);
    lv_label_set_text(ui_TH1, "12");
    lv_obj_add_flag(ui_TH1, LV_OBJ_FLAG_SCROLL_ONE);     /// Flags
    lv_obj_set_style_text_color(ui_TH1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_TH1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_TH1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_TH2 = lv_label_create(ui_Screen);
    lv_obj_set_width(ui_TH2, TH2_W);
    lv_obj_set_height(ui_TH2, TH2_H);
    lv_obj_set_x(ui_TH2, TH2_X);
    lv_obj_set_y(ui_TH2, TH2_Y);
    lv_obj_set_align(ui_TH2, LV_ALIGN_CENTER);
    lv_label_set_text(ui_TH2, "59");
    lv_obj_add_flag(ui_TH2, LV_OBJ_FLAG_SCROLL_ONE);     /// Flags
    lv_obj_set_style_text_color(ui_TH2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_TH2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_TH2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);


    ui_M1 = lv_label_create(ui_Screen);
    lv_obj_set_width(ui_M1, M1_W);
    lv_obj_set_height(ui_M1, M1_H);
    lv_obj_set_x(ui_M1, M1_X);
    lv_obj_set_y(ui_M1, M1_Y);
    lv_obj_set_align(ui_M1, LV_ALIGN_CENTER);
    lv_label_set_text(ui_M1, "25");
    lv_obj_add_flag(ui_M1, LV_OBJ_FLAG_SCROLL_ONE);     /// Flags
    lv_obj_set_style_text_color(ui_M1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_M1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_M1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ui_M1, LV_OBJ_FLAG_HIDDEN);


    ui_LabelMouth = lv_label_create(ui_Screen);
    lv_obj_set_width(ui_LabelMouth, MOUTH_W);
    lv_obj_set_height(ui_LabelMouth, MOUTH_H);
    lv_obj_set_x(ui_LabelMouth, MOUTH_X);
    lv_obj_set_y(ui_LabelMouth, MOUTH_Y);
    lv_obj_set_align(ui_LabelMouth, LV_ALIGN_CENTER);
    lv_label_set_text(ui_LabelMouth, "May 20");
    lv_obj_set_style_text_color(ui_LabelMouth, lv_color_hex(MOUTH_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_LabelMouth, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_LabelMouth, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_LabelMouth, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_LabelDate = lv_label_create(ui_Screen);
    lv_obj_set_width(ui_LabelDate, DATE_W);
    lv_obj_set_height(ui_LabelDate, DATE_H);
    lv_obj_set_x(ui_LabelDate, DATE_X);
    lv_obj_set_y(ui_LabelDate, DATE_Y);
    lv_obj_set_align(ui_LabelDate, LV_ALIGN_CENTER);
    lv_label_set_text(ui_LabelDate, "Wed");
    lv_obj_set_style_text_color(ui_LabelDate, lv_color_hex(DATE_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_LabelDate, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_LabelDate, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_LabelDate, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);

    // AM/PM label
    ui_LabelAmPm = lv_label_create(ui_Screen);
    lv_obj_set_width(ui_LabelAmPm, AMPM_W);
    lv_obj_set_height(ui_LabelAmPm, AMPM_H);
    lv_obj_set_x(ui_LabelAmPm, AMPM_X);
    lv_obj_set_y(ui_LabelAmPm, AMPM_Y);
    lv_obj_set_align(ui_LabelAmPm, LV_ALIGN_CENTER);
    lv_label_set_text(ui_LabelAmPm, "AM");
    lv_obj_set_style_text_color(ui_LabelAmPm, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_LabelAmPm, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_LabelAmPm, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_LabelAmPm, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);

    spaceImg = lv_img_create(ui_Screen);
    lv_img_set_src(spaceImg, manImage_map[0]);

#ifdef THEME_1
    lv_obj_set_style_text_font(ui_TH1, &clock_digits_56, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_TH2, &clock_digits_56, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_M1, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(spaceImg, LV_ALIGN_CENTER, 105, 10);
#endif

    // === Depth layer styles ===
#if DEPTH_INVERT
    #define _FAR_OPA  DEPTH_NEAR_OPA
    #define _NEAR_OPA DEPTH_FAR_OPA
#else
    #define _FAR_OPA  DEPTH_FAR_OPA
    #define _NEAR_OPA DEPTH_NEAR_OPA
#endif

    // Far layer opacity
    lv_obj_set_style_text_opa(ui_LabelMouth, _FAR_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_LabelDate, _FAR_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
    // Mid layer stays at 255 (already set)
    // Near layer opacity
    lv_obj_set_style_text_opa(ui_LabelAmPm, _NEAR_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);

#undef _FAR_OPA
#undef _NEAR_OPA

    // spaceImg alignment offset from lv_obj_align(spaceImg, LV_ALIGN_CENTER, 105, 10)
    spaceImg_base_y = 10;

}

void display_space(void)
{
    static int _spaceIndex = 1;
    if (NULL != ui_Screen && lv_scr_act() == ui_Screen)
    {
        lv_img_set_src(spaceImg, manImage_map[_spaceIndex]);
        #ifdef BUTTERFLY_60
        _spaceIndex = (_spaceIndex + 1) % 10;
        #endif

        #ifndef BUTTERFLY_60
        _spaceIndex++;
        if(_spaceIndex>=10)
             _spaceIndex = 1;
        #endif
    }
    delay(100);
}


// Day boost: brighten the warm-tinted labels and dim the star field so text
// cuts through the Pepper's ghost reflection in bright ambient light
void ui_set_day_boost(bool on)
{
    if (ui_Screen == NULL) return;
    lv_obj_set_style_text_color(ui_LabelMouth, lv_color_hex(on ? MOUTH_COLOR_DAY : MOUTH_COLOR),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelDate, lv_color_hex(on ? DATE_COLOR_DAY : DATE_COLOR),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_opa(ui_Screen, on ? BG_IMG_OPA_DAY : LV_OPA_COVER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}

void display_time(struct TimeStr timeInfo, lv_scr_load_anim_t anim_type)
{
    ui_change_sec(timeInfo.second);
    ui_change_min(timeInfo.minute);
    ui_change_h(timeInfo.hour, timeInfo.is_pm);
    ui_change_date(timeInfo.month, timeInfo.day, timeInfo.weekday);
}

void ui_init(void)
{
    lv_disp_t * dispp = lv_disp_get_default();
    lv_theme_t * theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                               false, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    ui_Screen_screen_init();
    lv_disp_load_scr(ui_Screen);
    depth_parallax_init();
}

void ui_release()
{
    depth_parallax_stop();

    // Stop notification timers/anims without deleting LVGL objects
    // (lv_obj_clean will handle child deletion)
    if (notify_dismiss_timer != NULL)
    {
        lv_timer_del(notify_dismiss_timer);
        notify_dismiss_timer = NULL;
    }
    if (ui_NotifyDot != NULL)
    {
        lv_anim_del(ui_NotifyDot, NULL);
    }
    if (led_breathe_timer != NULL)
    {
        lv_timer_del(led_breathe_timer);
        led_breathe_timer = NULL;
    }
    rgb.setBrightness(0.1).setRGB(0, 150, 0);
    ui_NotifyDot = NULL;
    ui_NotifyLabel = NULL;
    g_notification_visible = false;
    g_notification.pending = false;


    if (NULL != ui_Screen)
    {
        lv_obj_clean(ui_Screen);
        ui_Screen = NULL;
        ui_TH1 = NULL;
        ui_TH2 = NULL;
        ui_M1 = NULL;
        ui_Labelline = NULL;
        ui_LabelMouth = NULL;
        ui_LabelDate = NULL;
        ui_LabelAmPm = NULL;
    }

}

void ui_change_sec(int sec)
{
    String sec_str = "";
    if(sec<10)
        sec_str = "0"+String(sec);
    else
        sec_str = String(sec);
    lv_label_set_text(ui_M1, sec_str.c_str());
}

void ui_change_min(int mins)
{
    String sec_str = "";
    if(mins<10)
        sec_str = "0"+String(mins);
    else
        sec_str = String(mins);
    lv_label_set_text(ui_TH2, sec_str.c_str());
}

void ui_change_h(int h, bool is_pm)
{
    String sec_str = "";
    sec_str = String(h);
    lv_label_set_text(ui_TH1, sec_str.c_str());
    if (ui_LabelAmPm != NULL)
    {
        lv_label_set_text(ui_LabelAmPm, is_pm ? "PM" : "AM");
    }
}

void ui_change_date(int month, int day, int week)
{
    lv_label_set_text_fmt(ui_LabelMouth, "%s %d", mouthCh[month-1], day);
    lv_label_set_text(ui_LabelDate, weekDayCh[week]);
}

///////////////////// NOTIFICATION SYSTEM ////////////////////

static void notify_opa_anim_cb(void * obj, int32_t v)
{
    lv_obj_t * dot = (lv_obj_t *)obj;
    lv_obj_set_style_bg_opa(dot, (lv_opa_t)v, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(dot, (lv_opa_t)v, LV_PART_MAIN | LV_STATE_DEFAULT);
    // Use this dot's own child label, not the global (which may point to a newer notification)
    if (lv_obj_get_child_cnt(dot) > 0)
    {
        lv_obj_t * label = lv_obj_get_child(dot, 0);
        lv_obj_set_style_text_opa(label, (lv_opa_t)v, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void notify_exit_done_cb(lv_anim_t * anim)
{
    // Use the animation's own var (the dot it was started with),
    // not the global which may already point to a newer notification
    lv_obj_t * dot = (lv_obj_t *)anim->var;
    if (dot != NULL)
    {
        lv_obj_del(dot);
    }
    // Only null globals if they still reference the deleted object
    if (ui_NotifyDot == dot)
    {
        ui_NotifyDot = NULL;
        ui_NotifyLabel = NULL;
    }
}

static void notify_dismiss_timer_cb(lv_timer_t * timer)
{
    // LVGL auto-deletes this timer after firing (repeat_count=1),
    // so null the pointer before dismiss tries to delete it again
    notify_dismiss_timer = NULL;
    notification_dismiss();
}

static void led_breathe_cb(lv_timer_t * timer)
{
    led_breathe_phase += led_breathe_step;
    if (led_breathe_phase >= 1.0f)
    {
        led_breathe_phase = 1.0f;
        led_breathe_step = -0.25f;
    }
    else if (led_breathe_phase <= 0.0f)
    {
        led_breathe_phase = 0.0f;
        led_breathe_step = 0.25f;
    }
    float t = led_breathe_phase;
    uint8_t r = (uint8_t)(0   * (1-t) + led_breathe_r * t);
    uint8_t g = (uint8_t)(150 * (1-t) + led_breathe_g * t);
    uint8_t b = (uint8_t)(0   * (1-t) + led_breathe_b * t);
    rgb.setBrightness(0.15).setRGB(r, g, b);
}

static void notification_show()
{
    if (ui_Screen == NULL) return;

    // Create dot (circle container)
    ui_NotifyDot = lv_obj_create(ui_Screen);
    lv_obj_set_size(ui_NotifyDot, 80, 80);
    lv_obj_align(ui_NotifyDot, LV_ALIGN_BOTTOM_LEFT, 5, -30);
    lv_obj_set_style_radius(ui_NotifyDot, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_NotifyDot, lv_color_hex(g_notification.color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_NotifyDot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_NotifyDot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_NotifyDot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(ui_NotifyDot, LV_OBJ_FLAG_SCROLLABLE);

    // Add white shadow glow for visible halo on black background
    lv_obj_set_style_shadow_width(ui_NotifyDot, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_NotifyDot, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui_NotifyDot, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_NotifyDot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Create label inside dot
    ui_NotifyLabel = lv_label_create(ui_NotifyDot);
    lv_label_set_text(ui_NotifyLabel, g_notification.source);
    lv_obj_set_style_text_color(ui_NotifyLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_NotifyLabel, &lv_font_montserrat_32, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_NotifyLabel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(ui_NotifyLabel);

    // Enter animation: fade in bg_opa + shadow_opa + text_opa (no layer buffer needed)
    static lv_anim_t notify_enter_anim;
    lv_anim_init(&notify_enter_anim);
    lv_anim_set_var(&notify_enter_anim, ui_NotifyDot);
    lv_anim_set_exec_cb(&notify_enter_anim, notify_opa_anim_cb);
    lv_anim_set_values(&notify_enter_anim, 0, NOTIFY_MAX_OPA);
    lv_anim_set_time(&notify_enter_anim, 500);
    lv_anim_set_path_cb(&notify_enter_anim, lv_anim_path_ease_out);
    lv_anim_start(&notify_enter_anim);

    // Auto-dismiss timer (use custom duration or default)
    uint32_t timeout = g_notification.duration_ms > 0 ? g_notification.duration_ms : NOTIFY_DISPLAY_MS;
    notify_dismiss_timer = lv_timer_create(notify_dismiss_timer_cb, timeout, NULL);
    lv_timer_set_repeat_count(notify_dismiss_timer, 1);

    // LED breathing - extract RGB from notification color
    led_breathe_r = (g_notification.color >> 16) & 0xFF;
    led_breathe_g = (g_notification.color >> 8) & 0xFF;
    led_breathe_b = g_notification.color & 0xFF;
    led_breathe_phase = 0.0f;
    led_breathe_step = 0.25f;
    rgb.setBrightness(0.15).setRGB(0, 150, 0);

    if (led_breathe_timer != NULL)
    {
        lv_timer_del(led_breathe_timer);
    }
    led_breathe_timer = lv_timer_create(led_breathe_cb, 25, NULL);

    g_notification_visible = true;
    g_notification.pending = false;
}

void notification_receive(const char* source, uint32_t color, uint32_t duration_ms)
{
    // Dismiss old notification first (before setting new pending flag)
    if (g_notification_visible)
    {
        notification_dismiss();
    }

    strncpy(g_notification.source, source, NOTIFY_MAX_SOURCE_LEN - 1);
    g_notification.source[NOTIFY_MAX_SOURCE_LEN - 1] = '\0';
    g_notification.color = color;
    g_notification.duration_ms = duration_ms;
    g_notification.pending = true;
}

void notification_dismiss()
{
    if (notify_dismiss_timer != NULL)
    {
        lv_timer_del(notify_dismiss_timer);
        notify_dismiss_timer = NULL;
    }

    if (led_breathe_timer != NULL)
    {
        lv_timer_del(led_breathe_timer);
        led_breathe_timer = NULL;
    }

    // Restore idle LED color (green, matching wifi_init connected state)
    rgb.setBrightness(0.1).setRGB(0, 150, 0);

    // Exit animation: fade out, then delete in callback
    if (ui_NotifyDot != NULL)
    {
        lv_anim_del(ui_NotifyDot, NULL);
        static lv_anim_t notify_exit_anim;
        lv_anim_init(&notify_exit_anim);
        lv_anim_set_var(&notify_exit_anim, ui_NotifyDot);
        lv_anim_set_exec_cb(&notify_exit_anim, notify_opa_anim_cb);
        lv_anim_set_values(&notify_exit_anim, NOTIFY_MAX_OPA, 0);
        lv_anim_set_time(&notify_exit_anim, 500);
        lv_anim_set_path_cb(&notify_exit_anim, lv_anim_path_ease_in);
        lv_anim_set_ready_cb(&notify_exit_anim, notify_exit_done_cb);
        lv_anim_start(&notify_exit_anim);
    }

    g_notification_visible = false;
    g_notification.pending = false;
}

void notification_check_display()
{
    if (g_notification.pending && !g_notification_visible)
    {
        notification_show();
    }
}

///////////////////// DEBUG CALIBRATION SCREEN ////////////////////

static lv_obj_t * ui_DebugScreen = NULL;

static void debug_create_marker(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                 uint32_t color, const char *label_text)
{
    lv_obj_t *rect = lv_obj_create(parent);
    lv_obj_set_size(rect, 40, 40);
    lv_obj_set_pos(rect, x - 20, y - 20);
    lv_obj_set_style_bg_color(rect, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rect, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(rect, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(rect, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rect, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(rect);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl);
}

void ui_show_debug_screen(void)
{
    // Clean up existing debug screen if any
    if (ui_DebugScreen != NULL) {
        lv_obj_del(ui_DebugScreen);
        ui_DebugScreen = NULL;
    }

    ui_DebugScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_DebugScreen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_DebugScreen, 255, LV_PART_MAIN);
    lv_obj_clear_flag(ui_DebugScreen, LV_OBJ_FLAG_SCROLLABLE);

    // White 1px border rectangle at screen edges
    lv_obj_t *border = lv_obj_create(ui_DebugScreen);
    lv_obj_set_size(border, 238, 238);
    lv_obj_set_pos(border, 1, 1);
    lv_obj_set_style_bg_opa(border, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(border, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(border, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(border, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(border, 0, LV_PART_MAIN);
    lv_obj_clear_flag(border, LV_OBJ_FLAG_SCROLLABLE);

    // 5 colored markers at known positions
    debug_create_marker(ui_DebugScreen,  30,  30, 0xFF0000, "TL");  // Top-left, Red
    debug_create_marker(ui_DebugScreen, 210,  30, 0x00FF00, "TR");  // Top-right, Green
    debug_create_marker(ui_DebugScreen, 120, 120, 0xFFFFFF, "C");   // Center, White
    debug_create_marker(ui_DebugScreen,  30, 210, 0x0000FF, "BL");  // Bottom-left, Blue
    debug_create_marker(ui_DebugScreen, 210, 210, 0xFFFF00, "BR");  // Bottom-right, Yellow

    lv_disp_load_scr(ui_DebugScreen);
}

void ui_show_debug_grid(void)
{
    // Clean up existing debug screen if any
    if (ui_DebugScreen != NULL) {
        lv_obj_del(ui_DebugScreen);
        ui_DebugScreen = NULL;
    }

    ui_DebugScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_DebugScreen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_DebugScreen, 255, LV_PART_MAIN);
    lv_obj_clear_flag(ui_DebugScreen, LV_OBJ_FLAG_SCROLLABLE);

    // Markers at Y offsets: -100, -75, -50, -25, 0, +25, +50, +75, +100
    // Each uses LV_ALIGN_CENTER with X=0
    static const int16_t y_offsets[] = {-100, -75, -50, -25, 0, 25, 50, 75, 100};
    static const uint32_t colors[] = {
        0xFF0000, 0xFF8000, 0xFFFF00, 0x80FF00,
        0xFFFFFF,
        0x00FF80, 0x00FFFF, 0x0080FF, 0x0000FF
    };

    for (int i = 0; i < 9; i++) {
        lv_obj_t *lbl = lv_label_create(ui_DebugScreen);
        char buf[8];
        snprintf(buf, sizeof(buf), "%+d", y_offsets[i]);
        lv_label_set_text(lbl, buf);
        lv_obj_set_align(lbl, LV_ALIGN_CENTER);
        lv_obj_set_y(lbl, y_offsets[i]);
        lv_obj_set_x(lbl, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(colors[i]), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    }

    lv_disp_load_scr(ui_DebugScreen);
}

void ui_hide_debug_screen(void)
{
    if (ui_DebugScreen == NULL) return;

    // Load clock screen FIRST (must be active before deleting old one)
    if (ui_Screen != NULL) {
        lv_disp_load_scr(ui_Screen);
    }

    // Now safe to delete the former screen
    lv_obj_del(ui_DebugScreen);
    ui_DebugScreen = NULL;
}
