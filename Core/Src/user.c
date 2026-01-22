#include "user.h"
#include "motor.h"
#include "servo.h"
#include "echo.h"
#include "uart.h"
#include "usart.h"

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

void User_Code()
{
	if (mode == MANUAL)
	{
		if (uart_rx_buffer[0] == STOP)            //停止
			{Set_Motor(0, 0);	uart_rx_buffer[0] = 0x00;}
		else if (uart_rx_buffer[0] == MOVE_FORWARD)    //前进
			{Set_Motor(30, 30);	HAL_Delay(1000); uart_rx_buffer[0] = 0x00;}
		else if (uart_rx_buffer[0] == BACK_OFF)   //后退
			{Set_Motor(-30, -30); HAL_Delay(1000); uart_rx_buffer[0] = 0x00;}
		else if (uart_rx_buffer[0] == TURN_LEFT)   //左转
			{Set_Motor(-30, 30); HAL_Delay(1000); uart_rx_buffer[0] = 0x00;}
		else if (uart_rx_buffer[0] == TURN_RIGHT)   //右转
			{Set_Motor(30, -30); HAL_Delay(1000); uart_rx_buffer[0] = 0x00;}
		/*HAL_UART_Transmit(&huart2, 	//UART硬件的句柄的地址
						  uart_rx_buffer, 	 	//要发送的数据的地址
						  sizeof(uart_rx_buffer),				//要发送的数据大小(字节)
						  100);*/
	}
	else if(mode == AUTOMATIC)
	{
		//先检测正前方障碍物的距离
		Set_Angle(90);
		front_distance = Get_Median_Distance(3);
		if(front_distance > 0 && front_distance < 60 || front_distance == 0)   //前方障碍物距离较近
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
}