#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f1xx_hal.h"  // 根据你的芯片调整（如F4用stm32f4xx_hal.h）

// 替换标准库的u8/u16（HAL库无默认别名）
typedef unsigned char u8;
typedef unsigned short u16;

// -------------------------- 需修改的引脚配置 --------------------------
#define DHT11_PORT  GPIOB       // DHT11连接的GPIO端口
#define DHT11_PIN   GPIO_PIN_15 // DHT11连接的GPIO引脚
// ---------------------------------------------------------------------

// 函数声明
u8 DHT11_Init(void);          // DHT11初始化
u8 DHT11_Read_Data(u8 *temp, u8 *humi);  // 读取温湿度（temp=温度，humi=湿度）

#endif