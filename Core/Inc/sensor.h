#ifndef __SENSOR_H__
#define __SENSOR_H__

#include "stm32f1xx_hal.h"

extern uint16_t adc_buf[2];

#pragma pack(1)  // 强制1字节对齐，关闭自动填充
typedef struct {
    uint8_t humi;          // 湿度（1字节）
    uint8_t temp;          // 温度（1字节）
    int32_t encoder_left_cnt;  // 左编码器（4字节）
    int32_t encoder_right_cnt; // 右编码器（4字节）
    uint16_t pi_adc;   // 树莓派电压（4字节）
    uint16_t motor_adc;// 电机电压（4字节）
    uint16_t reserved;     // 保留字段（2字节）
} RawData_t;
#pragma pack()  // 恢复默认对齐

void Encoder_Read(void);
void Sensor_Read_And_Transmit(void);

extern uint8_t read_and_transmit;

#endif
