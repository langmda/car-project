#ifndef __SENSOR_H__
#define __SENSOR_H__

#include "stm32f1xx_hal.h"

extern uint16_t adc_buf[2];

/* 帧头：2字节，任选不常用的值 */
#define FRAME_HEADER_0   0xAA
#define FRAME_HEADER_1   0x55

#pragma pack(1)  // 强制1字节对齐，关闭自动填充

/* 负载部分 */
typedef struct {
	uint8_t  cmd;
    float    humi;
    float    temp;
    int16_t  encoder_left;
    int16_t  encoder_right;
    uint16_t pi_adc;
    uint16_t motor_adc;
    float    yaw;
} Payload_t;

/* 完整数据包 */
typedef struct {
    uint8_t  header[2];  // 帧头 0xAA 0x55
    uint16_t len;        // 数据长度（Payload 字节数）
    Payload_t data;      // 命令+传感器数据
    uint8_t  checksum;   // 校验和
} Frame_t;

#pragma pack()

void Encoder_Read(void);
void Read_And_Transmit(void);

#endif