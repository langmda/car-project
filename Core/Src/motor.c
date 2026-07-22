#include "motor.h"
#include "tim.h"

//设置电机转速函数，motor1和motor2范围-7200到7200
void Set_Motor(int16_t motor1, int16_t motor2)  
{
	if(motor1 > 7200){motor1 = 7200;}
	if(motor1 < -7200) {motor1 = -7200;}
	if(motor2 > 7200){motor2 = 7200;}
	if(motor2 < -7200) {motor2 = -7200;}
	//输出PWM
	if(motor1 >= 0)
	{
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, motor1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
	}
	else if(motor1 < 0)
	{
		motor1 = -motor1;
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, motor1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);
	}
	if(motor2 >= 0)
	{
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, motor2);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
	}
	else if(motor2 < 0)
	{
		motor2 = -motor2;
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, motor2);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
	}
}