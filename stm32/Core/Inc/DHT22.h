#ifndef __DHT22_H
#define __DHT22_H

#include "stm32f1xx_hal.h"
#include "delay.h"          // 需提供 delay_us / delay_ms 函数

/*************** 根据实际引脚修改 ****************/
#define DHT22_GPIO_PORT   GPIOB
#define DHT22_GPIO_PIN    GPIO_PIN_15
#define DHT22_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOB_CLK_ENABLE()
/************************************************/

#define OUT 1
#define IN  0

#define DHT22_Low   HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_RESET)
#define DHT22_High  HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET)

typedef unsigned char u8;
typedef unsigned short u16;   // 用于16位数据

// 函数声明
u8 DHT22_Init(void);
u8 DHT22_Read_Data(float *temp, float *humi);
u8 DHT22_Read_Byte(void);
u8 DHT22_Read_Bit(void);
void DHT22_Mode(u8 mode);
void DHT22_Rst(void);
u8 DHT22_Check(void);
// 中值滤波读取温湿度
// samples: 采样次数（建议奇数，如3、5、7），最大不超过10
// 返回值：0成功，3参数错误，4样本不足（通信失败太多）
u8 DHT22_Read_Data_Filtered(float *temp, float *humi, uint8_t samples);

#endif