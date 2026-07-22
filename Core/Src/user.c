#include "user.h"
#include "motor.h"
#include "servo.h"
#include "echo.h"
#include "uart.h"
#include "usart.h"
#include "sensor.h"
#include "pid.h"
#include "stdio.h"
#include "trajectory.h"
#include "infrared.h"
#include "avoidance.h"

#define CMD_TIMEOUT 300		 //指令超时时间，如果距离上一次收到指令超过该事件，就会将命令切换为停止
volatile uint32_t last_cmd_time = 0;  //上一次收到指令的时间
uint8_t last_cmd  = 0xFF;	 //上一次收到的指令 
uint8_t mode = MANUAL;		 //默认模式为手动操作
volatile uint8_t uart_rx_buffer[128];

uint8_t read_and_transmit = 0;		//读取并发送传感器数据的标志位
uint8_t read_angle = 0;				//读取mpu6050的角度数据

uint32_t leftPWM = 0, rightPWM = 0;

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
				PID_Reset_Yaw_Reference();
				Avoidance_Stop();
				break;
			case MOVE_FORWARD:
				Set_Motor(5000,  5000);
				break;
			case BACK_OFF:
				Set_Motor(-3000,  -3000);
				break;
			case TURN_LEFT:
				Set_Motor(-2800, 2800);
				break;
			case TURN_RIGHT:
				Set_Motor(2800, -2800);
				break;
			case CUSTOMIZATION:
				Set_Motor(leftPWM, rightPWM);
				break;
			case TRAJECTORY_Z:
				trajectory_Z();
				break;
			case TRAJECTORY_J:
				trajectory_J();
				break;
			case TRAJECTORY_U:
				trajectory_U();
				break;
			default:
				Set_Motor(0, 0);
				break;
		}
	}
	else if(mode == AUTOMATIC)
	{
		Maze_AbsoluteYaw();
	}
	else if (mode == LINE_FOLLOW)
	{
		Line_Follow_Task();   // 巡线主函数
	}
	
	if(read_and_transmit == 1)
	{
		Read_And_Transmit(); //读取传感器数据并发送
		read_and_transmit = 0;
	}
	/*
	uint32_t now_time = HAL_GetTick();
	static uint32_t last_time = 0;

	if (now_time - last_time >= 500) 
	{
		last_time = now_time;
		
		float pitch, roll, yaw;
		if (mpu_dmp_get_data(&pitch, &roll, &yaw) == 0) {
			// 归一化到 0~360
			if (yaw < 0) yaw += 360.0f;
			
			char msg[64];
			sprintf(msg, "yaw: %.1f  pitch: %.1f  roll: %.1f\r\n", yaw, pitch, roll);
			HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
		} else {
			char msg[] = "DMP read failed\r\n";
			HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
		}
	}
	*/
}