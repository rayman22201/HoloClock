#ifndef APP_PICTURE_H
#define APP_PICTURE_H

#include "sys/interface.h"

extern void picture_init();
extern void picture_process(const ImuAction *act_info);

#endif
