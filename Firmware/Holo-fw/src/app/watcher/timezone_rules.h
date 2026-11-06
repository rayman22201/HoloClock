#ifndef HOLO_TIMEZONE_RULES_H
#define HOLO_TIMEZONE_RULES_H

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct ClockTimezone
{
    char name[64] = "";
    char rule[64] = "UTC0";

    bool update(const char* zone, long offset)
    {
        if (!zone || !zone[0] || strlen(zone) >= sizeof(name) ||
            offset < -43200 || offset > 50400)
            return false;

        if (strcmp(zone, "America/Los_Angeles") == 0)
            strcpy(rule, "PST8PDT,M3.2.0,M11.1.0");
        else
        {
            // POSIX offsets have the opposite sign to seconds east of UTC.
            long magnitude = offset < 0 ? -offset : offset;
            snprintf(rule, sizeof(rule), "UTC%s%ld:%02ld:%02ld",
                     offset > 0 ? "-" : "+", magnitude / 3600,
                     (magnitude % 3600) / 60, magnitude % 60);
        }
        strcpy(name, zone);
        return true;
    }

    bool updateFromJson(const char* payload)
    {
        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, payload) || doc["status"] != "success" ||
            !doc["timezone"].is<const char*>() || !doc["offset"].is<long>())
            return false;
        return update(doc["timezone"].as<const char*>(), doc["offset"].as<long>());
    }
};

inline long effectiveUtcOffset(time_t epoch)
{
    struct tm local, utc;
    localtime_r(&epoch, &local);
    gmtime_r(&epoch, &utc);
    int days = local.tm_yday - utc.tm_yday;
    if (local.tm_year > utc.tm_year) days = 1;
    if (local.tm_year < utc.tm_year) days = -1;
    return days * 86400L + (local.tm_hour - utc.tm_hour) * 3600L +
           (local.tm_min - utc.tm_min) * 60L + local.tm_sec - utc.tm_sec;
}

#endif
