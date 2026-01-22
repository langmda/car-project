#include "user.h"
#include "motor.h"
#include "servo.h"
#include "echo.h"
#include "uart.h"
#include "usart.h"
#include "sensor.h"

//命令
#define STOP        0X00	//停止
#define MOVE_FORWARD 0X01	//前进
#define TURN_LEFT 0X02		//左转
#define TURN_RIGHT 0X03		//右转
#define BACK_OFF 0X04		//后退
#define MODE_SWITCH 0X05	//模式切换
#define LED_SWITCH 0x06     //灯光切换

//模式
#define AUTOMATIC  0x01		//自主避障模式
#define MANUAL     0x02		//手动操作模式

#define CMD_TIMEOUT 500
uint32_t last_cmd_time = 0;  // 最后一次收到指令的时间
uint32_t cmd_start_time = 0;	//指令开始时间
uint8_t current_cmd = STOP;  // 当前有效指令（默认停止）

void User_Code()
{
	if (HAL_GetTick() - last_cmd_time > CMD_TIMEOUT)
    {
        current_cmd = STOP;  // 超时→停止
    }
	if (mode == MANUAL)
	{
		switch(current_cmd)
		{
			case STOP:
				Set_Motor(0, 0);
				break;
			case MOVE_FORWARD:
				Set_Motor(30, 30);  // 持续前进（只要current_cmd是前进，就一直执行）
				break;
			case BACK_OFF:
				Set_Motor(-30, -30);
				break;
			case TURN_LEFT:
				Set_Motor(-30, 30);
				break;
			case TURN_RIGHT:
				Set_Motor(30, -30);
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
				Set_Motor(-30, -30);
				HAL_Delay(200);
				//停下
				Set_Motor(0, 0);
				HAL_Delay(200);
				//左转
				Set_Motor(-30, 30);
				HAL_Delay(100);
			}
			else                                //右侧障碍物距离较远，向右转
			{
				//后退
				Set_Motor(-30, -30);
				HAL_Delay(200);
				//停下
				Set_Motor(0, 0);
				HAL_Delay(200);
				//右转
				Set_Motor(30, -30);
				HAL_Delay(100);
			}
		}else       //前方障碍物距离较远
		{
			//前进
			Set_Motor(30, 30);
		}
	}
	if(read_and_transmit == 1)
	{
		Sensor_Read_And_Transmit(); //读取传感器数据并发送
	}
}