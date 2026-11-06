#include "watcher.h"
#include "ui.h"
#include "network.h"
#include "common.h"
#define ARDUINOJSON_USE_LONG_LONG 1
#include "ArduinoJson.h"
#include "timezone_rules.h"

#include <HTTPClient.h>

#include "Time.h"
#include "TimeLib.h"
#include "WiFiUdp.h"

#include <esp32-hal-timer.h>
#include <map>

void watch_init();
void watch_release();
void set_focus_update(int v);
void update_time_status();

WiFiUDP ntpUDP;
const char* ntpServer = "us.pool.ntp.org";
const long gmtOffset_sec = 0;
static ClockTimezone clockTimezone;
const char* timezoneServiceURL = "http://ip-api.com/json/?fields=status,timezone,offset";


struct DateTime {
  int sec;
  int min;
  int hour;
  int wday;
  int mday;
  int mon;
  int year;
};


#define TIME_API "http://api.m.taobao.com/rest/api3.do?api=mtop.common.gettimestamp"

// Epoch (2023-11) below which the system clock is considered never-synced
#define TIME_VALID_EPOCH        1700000000
// Clock face repaint cadence
#define DISPLAY_TICK_MS         1000
// Forced NTP re-sync: configTime() restarts SNTP and triggers an immediate query,
// so the clock is stepped at least hourly even if lwIP's background poll dies
#define NTP_RESYNC_INTERVAL_MS  (60UL * 60UL * 1000UL)
// Timezone/DST re-query from ip-api.com (bounded: 2s timeout, 2 attempts)
#define TZ_REFRESH_INTERVAL_MS  (24UL * 60UL * 60UL * 1000UL)

struct WT_Config
{
    unsigned long timeUpdataInterval;
};

struct WeatherAppRunData
{

    unsigned long preTimeMillis;
    unsigned long preFetchMillis;
    unsigned long preResyncMillis;
    unsigned long preTzRefreshMillis;
    unsigned long lastResyncMillis;
    long long preNetTimestamp;
    long long errorNetTimestamp;
    long long preLocalTimestamp;
    unsigned int coactusUpdateFlag;
    int clock_page;
    unsigned int update_type;

    BaseType_t xReturned_task_task_update;
    TaskHandle_t xHandle_task_task_update;

};

static WeatherAppRunData *run_data = NULL;

// Day-boost state (see brightness_poll below)
static unsigned long preBoostPollMillis = 0;
static int day_boost_override = -1;   // -1 = auto (schedule), 0 = force night, 1 = force day
static bool day_boost_active = false;
static bool day_boost_applied = false;

enum wea_event_Id
{
    UPDATE_NOW,
    UPDATE_NTP,
    UPDATE_DAILY
};

static long long getWorldTime()
{
    // The system clock is maintained by SNTP and keeps ticking on the crystal
    // even if WiFi drops, so once valid it is always the source of truth.
    time_t now = time(nullptr);
    if (now > TIME_VALID_EPOCH)
    {
        run_data->preNetTimestamp = (long long)now * 1000;
        run_data->preLocalTimestamp = millis();
        return run_data->preNetTimestamp;
    }
    // Never synced: free-run from the seed value, display only
    run_data->preNetTimestamp += (millis() - run_data->preLocalTimestamp);
    run_data->preLocalTimestamp = millis();
    return run_data->preNetTimestamp;
}

void getTimezone(String* timezone, uint32_t timeoutMs, int maxAttempts)
{
  HTTPClient http;
  int request_count = 0;
  String url = timezoneServiceURL;
  while(request_count<maxAttempts)
  {
      http.setTimeout(timeoutMs);
      http.begin(url);
      int httpResponseCode = http.GET();
      if (httpResponseCode == HTTP_CODE_OK)
      {
        String payload = http.getString();
        Serial.print("Timezone API response: ");
        Serial.println(payload);
        if (!clockTimezone.updateFromJson(payload.c_str()))
        {
            Serial.println("Invalid timezone response; keeping previous timezone.");
            http.end();
            request_count++;
            continue;
        }
        *timezone = clockTimezone.name;
        Serial.print("time area:");
        Serial.println(*timezone);
        Serial.print("timezone rule:");
        Serial.println(clockTimezone.rule);
        http.end();
        break;
      }
      else
      {
        Serial.print("Error getting timezone. HTTP error code: ");
        Serial.println(httpResponseCode);
      }
      http.end();
      request_count++;
  }

}


#ifdef ZH_TIME_ZONE
static long long get_timestamp()
{
    run_data->preNetTimestamp = run_data->preNetTimestamp + (millis() - run_data->preLocalTimestamp);
    run_data->preLocalTimestamp = millis();
    return run_data->preNetTimestamp;
}

static long long get_timestamp(String url)
{
    if (WL_CONNECTED == WiFi.status())
    {
        String time = "";
        HTTPClient http;
        http.setTimeout(1000);
        http.begin(url);

        int httpCode = http.GET();
        if (httpCode > 0)
        {
            if (httpCode == HTTP_CODE_OK)
            {
                String payload = http.getString();
                Serial.println(payload);
                int time_index = (payload.indexOf("data")) + 12;
                time = payload.substring(time_index, payload.length() - 3);
                run_data->preNetTimestamp = atoll(time.c_str()) + run_data->errorNetTimestamp + TIMEZERO_OFFSIZE;
                run_data->preLocalTimestamp = millis();
            }
        }
        else
        {
            Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
            run_data->preNetTimestamp = run_data->preNetTimestamp + (millis() - run_data->preLocalTimestamp);
            run_data->preLocalTimestamp = millis();
        }
        http.end();

    }
    else{
            run_data->preNetTimestamp = run_data->preNetTimestamp + (millis() - run_data->preLocalTimestamp);
            run_data->preLocalTimestamp = millis();
    }
    return run_data->preNetTimestamp;
}
#endif


// Pure formatter: converts an epoch-ms value to local time and repaints the
// clock face. Never writes the system clock — SNTP owns it.
static void UpdateTime_RTC(long long timestamp)
{
    struct TimeStr t;
    time_t secs = (time_t)(timestamp / 1000);
    struct tm ti;
    localtime_r(&secs, &ti);

    t.month = ti.tm_mon + 1;
    t.day = ti.tm_mday;

    int hour24 = ti.tm_hour;
    t.is_pm = (hour24 >= 12);
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    t.hour = hour12;

    t.minute = ti.tm_min;
    t.second = ti.tm_sec;
    t.weekday = ti.tm_wday;
    display_time(t, LV_SCR_LOAD_ANIM_NONE);
}

//use the forigner timer server
static void UpdateTime_RTC_EN()
{
   UpdateTime_RTC(getWorldTime());
}

void watch_init()
{
    ui_init();
    run_data = (WeatherAppRunData *)calloc(1, sizeof(WeatherAppRunData));
    run_data->preNetTimestamp = 1679317985522;
    run_data->errorNetTimestamp = 2;
    run_data->preLocalTimestamp = millis();
    run_data->clock_page = 0;
    run_data->preTimeMillis = 0;
    run_data->preFetchMillis = millis();
    run_data->preResyncMillis = millis();
    run_data->preTzRefreshMillis = millis();
    run_data->lastResyncMillis = 0;
    run_data->coactusUpdateFlag = 0x01;
    run_data->update_type = 0x00;
    // ui_init() above recreates labels at night defaults; force the next
    // brightness poll to re-apply the active day-boost styling
    day_boost_applied = false;
}

void set_focus_update(int v)
{
    run_data->coactusUpdateFlag = v;
    #ifdef EN_TIME_ZONE
    if (WL_CONNECTED == WiFi.status())
    {
        String timezone;
        getTimezone(&timezone, 5000, 5);
        configTzTime(clockTimezone.rule, ntpServer);
        run_data->lastResyncMillis = millis();
        int retries = 0;
        while (time(nullptr) < TIME_VALID_EPOCH && retries < 15)
        {
            delay(1000);
            retries++;
        }
        if (time(nullptr) > TIME_VALID_EPOCH)
        {
            Serial.println("Time is synchronized.");
        }
        else
        {
            Serial.println("NTP sync failed after 15s timeout.");
        }
    }

    #endif
}

void getTimeDebugJson(String* out)
{
    StaticJsonDocument<1024> doc;

    if (WiFi.status() == WL_CONNECTED)
        doc["wifi_status"] = "connected";
    else
        doc["wifi_status"] = "disconnected";

    doc["ip"] = WiFi.localIP().toString();
    doc["ntp_server"] = ntpServer;
    time_t now = time(nullptr);
    doc["tz_offset_sec"] = effectiveUtcOffset(now);
    doc["timezone"] = clockTimezone.name;
    doc["tz_rule"] = clockTimezone.rule;
    struct tm local;
    localtime_r(&now, &local);
    char localTime[32];
    strftime(localTime, sizeof(localTime), "%Y-%m-%d %H:%M:%S", &local);
    doc["local_time"] = localTime;
    doc["is_dst"] = local.tm_isdst > 0;
    doc["time_raw"] = (long)now;
    doc["time_valid"] = (now > 1700000000);

    if (run_data != NULL)
    {
        doc["pre_net_timestamp"] = run_data->preNetTimestamp;
        doc["last_resync_ms"] = run_data->lastResyncMillis;
    }
    else
    {
        doc["pre_net_timestamp"] = 0;
        doc["last_resync_ms"] = 0;
    }

    doc["day_boost"] = day_boost_active;
    doc["day_boost_mode"] = (day_boost_override < 0) ? "auto" : (day_boost_override ? "on" : "off");
    doc["uptime_ms"] = millis();

    serializeJson(doc, *out);
}

void watch_release()
{
    ui_release();
}

// ── Day-boost: max brightness/contrast for daytime Pepper's ghost readability ──
// No ambient light sensor is populated on this hardware (verified via /i2c-debug:
// only the MPU6050 ACKs on the bus), so day mode follows a local-time schedule,
// with an HTTP override (/day-boost?v=on|off|auto).
#define DAY_BOOST_POLL_MS   2000
#define DAY_START_HOUR      7    // boost from 07:00 local...
#define DAY_END_HOUR        20   // ...until 20:00 local
#define BACKLIGHT_DAY       1.0f
#define BACKLIGHT_NIGHT     0.95f

void set_day_boost_override(int mode)
{
    day_boost_override = mode;
}

static void brightness_poll()
{
    if (!doDelayMillisTime(DAY_BOOST_POLL_MS, &preBoostPollMillis, false))
        return;

    bool want = false;
    if (day_boost_override >= 0)
    {
        want = (day_boost_override == 1);
    }
    else
    {
        // Schedule only applies once the clock is trustworthy; night look otherwise
        time_t now = time(nullptr);
        if (now > TIME_VALID_EPOCH)
        {
            struct tm ti;
            localtime_r(&now, &ti);
            want = (ti.tm_hour >= DAY_START_HOUR && ti.tm_hour < DAY_END_HOUR);
        }
    }

    if (want != day_boost_active || !day_boost_applied)
    {
        day_boost_active = want;
        day_boost_applied = true;
        ui_set_day_boost(day_boost_active);
        Serial.printf("Day boost %s (%s)\n", day_boost_active ? "on" : "off",
                      day_boost_override < 0 ? "schedule" : "override");
    }
    // Re-assert every poll, not only on transitions: exitSleep() hardcodes 95%,
    // so a daytime wake must recover to 100% within one poll
    screen.setBackLight(day_boost_active ? BACKLIGHT_DAY : BACKLIGHT_NIGHT);
}

void update_time_status()
{
    // Repaint the clock face once per second from the system clock (or the
    // extrapolated fallback while never-synced)
    if (0x01 == run_data->coactusUpdateFlag || doDelayMillisTime(DISPLAY_TICK_MS, &run_data->preTimeMillis, false))
    {
        run_data->coactusUpdateFlag = 0x00;

        #ifdef ZH_TIME_ZONE
        if (doDelayMillisTime(900000, &run_data->preFetchMillis, false))
            UpdateTime_RTC(get_timestamp(TIME_API));
        else
            UpdateTime_RTC(get_timestamp());
        #endif

        #ifdef EN_TIME_ZONE
        UpdateTime_RTC_EN();
        #endif
    }

    #ifdef EN_TIME_ZONE
    // Forced hourly NTP re-sync (non-blocking): keeps the clock stepped even if
    // lwIP's background SNTP poll silently dies
    if (doDelayMillisTime(NTP_RESYNC_INTERVAL_MS, &run_data->preResyncMillis, false))
    {
        if (WL_CONNECTED == WiFi.status())
        {
            configTzTime(clockTimezone.rule, ntpServer);
            run_data->lastResyncMillis = millis();
            Serial.println("Hourly NTP re-sync requested.");
        }
    }

    // Daily timezone/DST refresh (bounded: 2s timeout, 2 attempts, ~4s worst case)
    if (doDelayMillisTime(TZ_REFRESH_INTERVAL_MS, &run_data->preTzRefreshMillis, false))
    {
        if (WL_CONNECTED == WiFi.status())
        {
            String timezone;
            getTimezone(&timezone, 2000, 2);
            configTzTime(clockTimezone.rule, ntpServer);
            run_data->lastResyncMillis = millis();
            Serial.println("Daily timezone refresh done.");
        }
    }
    #endif

    brightness_poll();
    display_space();
    notification_check_display();
    delay(100);
}
