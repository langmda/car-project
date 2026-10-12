#ifndef _TRAJECTORY_H
#define _TRAJECTORY_H

#include "stm32f1xx_hal.h"

extern float pitch, roll, current_yaw;
void turn_angle_blocking(int16_t target_angle_deg);
void go_straight_blocking(int32_t distance_mm);
void arc_blocking(int16_t delta_angle_deg, int16_t forward_speed);
void trajectory_Z();
void trajectory_J();
void trajectory_U();
	
#endif
