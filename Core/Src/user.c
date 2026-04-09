#include "user.h"
#include "motor.h"
#include "servo.h"
#include "echo.h"
#include "uart.h"
#include "usart.h"
#include "sensor.h"
#include "pid.h"
#include "stdio.h"

#define CMD_TIMEOUT 300		 //指令超时时间，如果距离上一次收到指令超过该事件，就会将命令切换为停止
volatile uint32_t last_cmd_time = 0;  //上一次收到指令的时间
uint8_t last_cmd  = 0xFF;	 //上一次收到的指令 
uint8_t mode = MANUAL;		 //默认模式为手动操作
volatile uint8_t uart_rx_buffer[128];

void User_Code()
{	
	
	if (mode == MANUAL)
	{
		if (HAL_GetTick() - last_cmd_time > CMD_TIMEOUT)	//指令超时处理
		{
			PI_Param_Clear();  
			current_cmd = STOP;  // 超时→停止
		}
		
		if( last_cmd != current_cmd)
		{
			PI_Param_Clear();
			last_cmd = current_cmd;
		}
		
		switch(current_cmd)
		{
			case STOP:
				Set_Motor(0, 0);
				break;
			case MOVE_FORWARD:
				PID_Go_Straight(2000);
				break;
			case BACK_OFF:
				Set_Motor(-2000, -2000);
				break;
			case TURN_LEFT:
				Set_Motor(-1500, 1500);
				break;
			case TURN_RIGHT:
				Set_Motor(1500, -1500);
				break;
			default:
				Set_Motor(0, 0);
				break;
		}
	}
	else if(mode == AUTOMATIC)
	{
		//先检测正前方障碍物的距离
		Set_Angle(90);
		front_distance = Get_Median_Distance(3);
		if(front_distance > 0 && front_distance < 60)   //前方障碍物距离较近
		{
			Set_Motor(0, 0);
			Get_Left_Right_Distance();
			if(left_distance > (right_distance + 10))   //左侧障碍物距离较远，向左转
			{
				//后退
				Set_Motor(-2000, -2000);
				HAL_Delay(200);
				//停下
				Set_Motor(0, 0);
				HAL_Delay(100);
				//左转
				Set_Motor(-2000, 2000);
				HAL_Delay(200);
			}
			else                                //右侧障碍物距离较远，向右转
			{
				//后退
				Set_Motor(-2000, -2000);
				HAL_Delay(200);
				//停下
				Set_Motor(0, 0);
				HAL_Delay(100);
				//右转
				Set_Motor(2000, -2000);
				HAL_Delay(200);
			}
		}else       //前方障碍物距离较远
		{
			//前进
			Set_Motor(2000, 2000);
		}
	}
	if(read_and_transmit == 1)
	{
		Read_And_Transmit(); //读取传感器数据并发送
		read_and_transmit = 0;
	}
	/*
	sprintf(buffer, "KP:%f\r\nKI:%f\r\nIFil:%f\r\nILim:%d\r\nPWMLim:%d\r\n", Kp,Ki,I_Filtering,I_Limit,PWM_Limit);
	HAL_UART_Transmit(&huart2, (uint8_t*)buffer, strlen(buffer), 100);
	HAL_Delay(1000);
	*/
}