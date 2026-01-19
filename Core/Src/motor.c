#include "motor.h"
#include "tim.h"

//设置电机转速函数，motor1和motor2范围-100到100
void Set_Motor(int8_t motor1, int8_t motor2)  
{
	//将参数限制在-100到100之间
	if(motor1 > 100){motor1 = 100;}
	if(motor1 < -100) {motor1 = -100;}
	if(motor2 > 100){motor2 = 100;}
	if(motor2 < -100) {motor2 = -100;}
	//输出PWM
	if(motor1 >= 0)
	{
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, motor1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
	}
	else if(motor1 < 0)
	{
		motor1 = -motor1;
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, motor1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);
	}
	if(motor2 >= 0)
	{
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, motor2);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
	}
	else if(motor2 < 0)
	{
		motor2 = -motor2;
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, motor2);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
	}
}
