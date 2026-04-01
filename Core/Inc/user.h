#ifndef _USER_H
#define _USER_H

#include "stm32f1xx_hal.h"

void User_Code(void);

//命令
#define STOP        0X00	//停止
#define MOVE_FORWARD 0X01	//前进
#define TURN_LEFT 0X02		//左转
#define TURN_RIGHT 0X03		//右转
#define BACK_OFF 0X04		//后退
#define MODE_SWITCH 0X05	//模式切换
#define LED_SWITCH 0x06     //灯光切换
#define PID_PARAM 0x0E		//PID参数调试
#define SENSOR_DATA	0x11	//传感器数据

//模式
#define AUTOMATIC  0x01		//自主避障模式
#define MANUAL     0x02		//手动操作模式

extern volatile uint32_t last_cmd_time;
extern volatile uint8_t uart_rx_buffer[128];
extern uint8_t mode;
extern volatile uint8_t current_cmd;

#endif
