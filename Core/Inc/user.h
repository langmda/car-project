#ifndef _USER_H
#define _USER_H

#include "stm32f1xx_hal.h"

void User_Code(void);

extern uint32_t last_cmd_time;
extern uint32_t cmd_start_time;
extern uint8_t current_cmd;  // 当前有效指令（默认停止）

#endif
