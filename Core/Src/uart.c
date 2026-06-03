#include "uart.h"
#include "usart.h"
#include "user.h"
#include "pid.h"
#include "main.h"
#include "stdio.h"
#include "servo.h"
#include "avoidance.h"

volatile uint8_t current_cmd = STOP;	//当前有效命令
volatile uint8_t led_counter = 0;    //led计数器

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2)
    {
        last_cmd_time = HAL_GetTick();

        if(uart_rx_buffer[0] == MODE_SWITCH)   //模式切换
        {
			Avoidance_Stop();
            if(mode == MANUAL)  {mode = AUTOMATIC;}
            else                {mode = MANUAL;}
            uart_rx_buffer[0] = STOP;
        }
        else if(uart_rx_buffer[0] == LED_SWITCH)    //灯光切换
        {
            if(led_counter == 0)
            {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);;
                led_counter = 1;
            }
            else if(led_counter == 1)
            {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
                led_counter = 0;
            }
            uart_rx_buffer[0] = STOP;
        }	
		else if (uart_rx_buffer[0] == PID_PARAM) 
		{
			PIDParams_t *pid = (PIDParams_t*)uart_rx_buffer;
			Kp = pid->Kp;
			Ki = pid->Ki;
			I_Limit = pid->I_Lim;
			PWM_Limit = pid->PWM_Lim;
		}   
		else if (uart_rx_buffer[0] == LINE_FOLLOW_MODE_SWITCH) 
		{
			if (mode == LINE_FOLLOW)
			{
				mode = MANUAL;
				PI_Param_Clear();
				uart_rx_buffer[0] = STOP;
			}
			else
			{
				mode = LINE_FOLLOW;
				PI_Param_Clear();
				uart_rx_buffer[0] = STOP;
			}
		}   
		else 
		{
            current_cmd = uart_rx_buffer[0];	// 普通运动命令，存入变量供主循环使用
        }
    }
	HAL_UARTEx_ReceiveToIdle_IT(huart, uart_rx_buffer, sizeof(uart_rx_buffer));
    __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);
}