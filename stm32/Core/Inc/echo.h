#ifndef _ECHO_H
#define _ECHO_H

#include "stm32f1xx_hal.h"

extern float left_distance;		
extern float right_distance;
extern float front_distance;

void delay_us(uint32_t us);
void Trig(void);
void Wait_Echo_Measure(void);
float Get_Median_Distance(uint8_t count);
void Get_Left_Right_Distance();

#endif
