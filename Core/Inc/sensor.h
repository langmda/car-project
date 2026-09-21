#ifndef __SENSOR_H__
#define __SENSOR_H__

#include "stm32f1xx_hal.h"

extern uint16_t adc_buf[2];

#pragma pack(1)  // 强制1字节对齐，关闭自动填充
typedef struct {
	uint8_t cmd;			
    float humi;          // 湿度（1字节）
    float temp;          // 温度（1字节）
    int16_t encoder_left;  // 左编码器（2字节）
    int16_t encoder_right; // 右编码器（2字节）
    uint16_t pi_adc;   // 树莓派电压（2字节）
    uint16_t motor_adc;// 电机电压（2字节）
	float yaw; 
} RawData_t;
#pragma pack()  // 恢复默认对齐

void Encoder_Read(void);
void Read_And_Transmit(void);


#endif