#ifndef __SENSOR_H__
#define __SENSOR_H__

#include "stm32f1xx_hal.h"

extern uint32_t adc_buf[2];

// 传输给手机的数据结构体
typedef struct {
    // DHT11原始值
    uint8_t humi;   // 湿度整数（如80）
    uint8_t temp;   // 温度整数（如25）
    // 编码器原始值（1秒内的脉冲数）
    int32_t encoder_left_cnt; // 左电机脉冲数
    int32_t encoder_right_cnt;// 右电机脉冲数
    // ADC原始值（0~4095）
    uint32_t pi_bat_voltage;    // 电池1 ADC值
    uint32_t motor_bat_voltage;    // 电池2 ADC值
    uint16_t reserved;         // 对齐（可选）
} RawData_t;

void Encoder_Read(void);
void Sensor_Read_And_Transmit(void);

extern uint8_t read_and_transmit;

#endif
