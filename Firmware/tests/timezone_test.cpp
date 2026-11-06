#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifndef HOLO_DST_BASELINE
#include "../Holo-fw/src/app/watcher/timezone_rules.h"
#endif

static int failures = 0;
static void check(time_t epoch, const char* expected, int dst, long offset)
{
    tm local;
    localtime_r(&epoch, &local);
    char result[32];
    strftime(result, sizeof(result), "%Y-%m-%d %H:%M:%S", &local);
    if (strcmp(result, expected) || local.tm_isdst != dst) {
        fprintf(stderr, "FAIL: %ld -> %s dst=%d; expected %s dst=%d\n",
                (long)epoch, result, local.tm_isdst, expected, dst);
        ++failures;
    }
#ifndef HOLO_DST_BASELINE
    assert(effectiveUtcOffset(epoch) == offset);
#else
    (void)offset;
#endif
}

int main()
{
#ifdef HOLO_DST_BASELINE
    // Arduino configTime(-25200, 0, ...) creates this fixed summer offset.
    setenv("TZ", "UTC7DST7", 1);
#else
    ClockTimezone zone;
    assert(zone.update("America/Los_Angeles", -25200));
    setenv("TZ", zone.rule, 1);
#endif
    tzset();
    check(1772963999, "2026-03-08 01:59:59", 0, -28800);
    check(1772964000, "2026-03-08 03:00:00", 1, -25200);
    check(1793523599, "2026-11-01 01:59:59", 1, -25200);
    check(1793523600, "2026-11-01 01:00:00", 0, -28800);
#ifndef HOLO_DST_BASELINE
    // A winter API snapshot must select the same year-round rules.
    assert(zone.update("America/Los_Angeles", -28800));
    assert(strcmp(zone.rule, "PST8PDT,M3.2.0,M11.1.0") == 0);
    const char* invalid[] = {
        "{", "{}", "{\"status\":\"fail\"}",
        "{\"status\":\"success\",\"timezone\":\"America/Los_Angeles\"}",
        "{\"status\":\"success\",\"timezone\":\"\",\"offset\":0}",
        "{\"status\":\"success\",\"timezone\":\"Etc/UTC\",\"offset\":\"0\"}",
        "{\"status\":\"success\",\"timezone\":\"Etc/UTC\",\"offset\":999999}"
    };
    for (const char* payload : invalid) {
        assert(!zone.updateFromJson(payload));
        assert(strcmp(zone.name, "America/Los_Angeles") == 0);
        assert(strcmp(zone.rule, "PST8PDT,M3.2.0,M11.1.0") == 0);
    }
    assert(zone.updateFromJson("{\"status\":\"success\",\"timezone\":\"America/Los_Angeles\",\"offset\":-28800}"));
    assert(!zone.update(nullptr, 0));
    assert(!zone.update("", 0));
    assert(!zone.update("America/Los_Angeles", 999999));
    assert(strcmp(zone.rule, "PST8PDT,M3.2.0,M11.1.0") == 0);
    assert(zone.update("Asia/Kolkata", 19800));
    setenv("TZ", zone.rule, 1); tzset();
    check(1767225600, "2026-01-01 05:30:00", 0, 19800);
    assert(zone.update("Etc/Unknown", -12600));
    setenv("TZ", zone.rule, 1); tzset();
    check(1767225600, "2025-12-31 20:30:00", 0, -12600);
    assert(zone.update("Etc/Unknown", 1800));
    setenv("TZ", zone.rule, 1); tzset();
    check(1767225599, "2026-01-01 00:29:59", 0, 1800);
    assert(zone.update("Etc/UTC", 0));
    setenv("TZ", zone.rule, 1); tzset();
    check(1767225600, "2026-01-01 00:00:00", 0, 0);
#endif
    printf("Timezone checks: %d failures\n", failures);
    return failures ? 1 : 0;
}
