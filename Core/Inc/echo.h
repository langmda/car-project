#ifndef _ECHO_H
#define _ECHO_H

#include "stm32f1xx_hal.h"

void delay_us(uint32_t us);
void Trig(void);
void Wait_Echo_Measure();

#endif
