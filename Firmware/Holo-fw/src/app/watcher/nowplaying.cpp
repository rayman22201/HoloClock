#include "nowplaying.h"
#include "ui.h"
#include "common.h"
#include "Arduino.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#define ARDUINOJSON_USE_LONG_LONG 1
#include "ArduinoJson.h"

// --- State ---
static lv_obj_t *ticker_label = NULL;
static char ticker_text[128];
static unsigned long lastPollMillis = 0;
static unsigned long pollInterval = 30000;
static bool ticker_visible = false;
static bool ticker_has_data = false;

// --- Diagnostics (exposed via /np-debug) ---
static int np_last_code = 0;            // last HTTP result (or 0 = none yet)
static char np_last_err[32] = "";       // last JSON/parse error, "" if none
static unsigned int np_poll_count = 0;  // fetch attempts since boot
static unsigned int np_ok_count = 0;    // successful title updates
static unsigned int np_fail_streak = 0; // consecutive failed fetches
static unsigned long np_last_ok_millis = 0;      // millis() of last success
static unsigned long np_last_attempt_millis = 0; // millis() of last attempt
static unsigned int np_wifi_hide_count = 0;      // times hidden due to WiFi loss
static unsigned long np_last_wifi_hide_millis = 0;

// --- Fade helper ---
static void ticker_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)v, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void ticker_fade(lv_opa_t from, lv_opa_t to, uint32_t duration)
{
    if (ticker_label == NULL) return;
    lv_anim_del(ticker_label, ticker_opa_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ticker_label);
    lv_anim_set_exec_cb(&a, ticker_opa_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, duration);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// --- Public API ---

void nowplaying_create(lv_obj_t *parent)
{
    ticker_label = lv_label_create(parent);
    lv_obj_set_width(ticker_label, TICKER_W);
    lv_obj_set_height(ticker_label, TICKER_H);
    lv_obj_set_x(ticker_label, TICKER_X);
    lv_obj_set_y(ticker_label, TICKER_Y);
    lv_obj_set_align(ticker_label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(ticker_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ticker_label, lv_color_hex(TICKER_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ticker_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(ticker_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_speed(ticker_label, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ticker_label, "");

    lastPollMillis = 0;
    pollInterval = 30000;
    ticker_visible = false;
    ticker_has_data = false;
}

void nowplaying_destroy(void)
{
    if (ticker_label != NULL)
    {
        lv_anim_del(ticker_label, NULL);
    }
    ticker_label = NULL;
    ticker_visible = false;
    ticker_has_data = false;
}

void nowplaying_poll(void)
{
    if (ticker_label == NULL) return;

    // WiFi not connected — fade out
    if (WL_CONNECTED != WiFi.status())
    {
        Serial.println("NP: no WiFi");
        if (ticker_visible)
        {
            ticker_fade(255, 0, 600);
            ticker_visible = false;
            np_wifi_hide_count++;
            np_last_wifi_hide_millis = millis();
        }
        return;
    }

    // Timer check
    if (!doDelayMillisTime(pollInterval, &lastPollMillis, false))
        return;

    np_poll_count++;
    np_last_attempt_millis = millis();

    // --- Fetch Rainwave API ---
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(3000);
    http.begin(client, "https://rainwave.cc/api4/info?sid=4");

    int code = http.GET();
    Serial.printf("NP: HTTP %d\n", code);
    np_last_code = code;
    if (code != HTTP_CODE_OK)
    {
        http.end();
        pollInterval = 30000;
        np_fail_streak++;
        return;
    }

    String payload = http.getString();
    http.end();
    Serial.printf("NP: payload %d bytes\n", payload.length());

    // Filter: only parse the fields we need to keep memory low
    StaticJsonDocument<256> filter;
    filter["sched_current"]["end"] = true;
    filter["sched_current"]["songs"][0]["title"] = true;
    filter["sched_current"]["songs"][0]["artists"][0]["name"] = true;

    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, payload,
        DeserializationOption::Filter(filter));
    if (err)
    {
        Serial.printf("NP: JSON err: %s\n", err.c_str());
        pollInterval = 30000;
        np_fail_streak++;
        snprintf(np_last_err, sizeof(np_last_err), "json:%s", err.c_str());
        return;
    }

    JsonObject sched = doc["sched_current"];
    if (sched.isNull())
    {
        pollInterval = 30000;
        np_fail_streak++;
        snprintf(np_last_err, sizeof(np_last_err), "no sched_current");
        return;
    }

    JsonArray songs = sched["songs"];
    if (songs.isNull() || songs.size() == 0)
    {
        pollInterval = 30000;
        np_fail_streak++;
        snprintf(np_last_err, sizeof(np_last_err), "no songs");
        return;
    }

    const char *title = songs[0]["title"] | "";
    const char *artist = "";
    JsonArray artists = songs[0]["artists"];
    if (!artists.isNull() && artists.size() > 0)
        artist = artists[0]["name"] | "";

    snprintf(ticker_text, sizeof(ticker_text), LV_SYMBOL_AUDIO " %s - %s", artist, title);
    Serial.printf("NP: => %s\n", ticker_text);
    lv_label_set_text(ticker_label, ticker_text);

    if (!ticker_visible)
    {
        ticker_fade(0, 255, 800);
        ticker_visible = true;
    }
    ticker_has_data = true;
    np_ok_count++;
    np_fail_streak = 0;
    np_last_err[0] = '\0';
    np_last_ok_millis = millis();

    // Smart polling: re-poll 2s after song end
    long long end_epoch = sched["end"] | 0LL;
    if (end_epoch > 0)
    {
        long long now_epoch = (long long)time(nullptr);
        long long remaining = end_epoch - now_epoch;
        if (remaining > 0 && remaining < 600)
            pollInterval = (unsigned long)(remaining + 2) * 1000;
        else
            pollInterval = 30000;
    }
    else
    {
        pollInterval = 30000;
    }
}

void nowplaying_debug_json(String* out)
{
    StaticJsonDocument<512> doc;
    doc["visible"] = ticker_visible;
    doc["has_data"] = ticker_has_data;
    doc["text"] = ticker_text;
    doc["poll_interval_ms"] = pollInterval;
    doc["poll_count"] = np_poll_count;
    doc["ok_count"] = np_ok_count;
    doc["fail_streak"] = np_fail_streak;
    doc["last_http_code"] = np_last_code;
    doc["last_err"] = np_last_err;
    doc["last_ok_age_ms"] = np_last_ok_millis ? (millis() - np_last_ok_millis) : 0;
    doc["last_attempt_age_ms"] = np_last_attempt_millis ? (millis() - np_last_attempt_millis) : 0;
    doc["wifi_hide_count"] = np_wifi_hide_count;
    doc["last_wifi_hide_age_ms"] = np_last_wifi_hide_millis ? (millis() - np_last_wifi_hide_millis) : 0;
    serializeJson(doc, *out);
}

void nowplaying_parallax_start(int16_t base_y, int16_t amp, uint32_t period_ms)
{
    if (ticker_label == NULL) return;
    start_float_anim(ticker_label, base_y, amp, period_ms);
}

void nowplaying_parallax_stop(void)
{
    if (ticker_label == NULL) return;
    lv_anim_del(ticker_label, float_y_cb);
}
