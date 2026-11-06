#include "picture.h"
#include "common.h"
#include "../watcher/watcher.h"

void picture_init()
{
    watch_init();
    set_focus_update(1);
    tft->setSwapBytes(true);
}

void picture_process(const ImuAction *act_info)
{
    update_time_status();
}
