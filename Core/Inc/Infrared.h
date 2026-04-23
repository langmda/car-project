#ifndef __INFRARED_H
#define __INFRARED_H

#include "stm32f1xx_hal.h" 

HAL_StatusTypeDef Read_IR_Sensor(uint8_t *pSensorData);
void Line_Follow_Task(void);
static uint8_t Handle_Edge_Turn(uint8_t *sensor);
static float Get_Position_Error(uint8_t *sensor);

#endif