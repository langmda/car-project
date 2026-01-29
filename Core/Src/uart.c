#include "uart.h"
#include "usart.h"
#include "user.h"

uint8_t led_counter = 0;    //led计数器

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        last_cmd_time = HAL_GetTick();

        if(uart_rx_buffer[0] == MODE_SWITCH)   //模式切换
        {
            if(mode == MANUAL)  {mode = AUTOMATIC;}
            else                {mode = MANUAL;}
            uart_rx_buffer[0] = STOP;
        }
        else if(uart_rx_buffer[0] == LED_SWITCH)    //灯光切换
        {
            if(led_counter == 0)
            {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
                led_counter = 1;
            }
            else if(led_counter == 1)
            {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
                led_counter = 2;
            }
            else if(led_counter == 2)
            {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
                led_counter = 3;
            }
			else if(led_counter == 3)
			{
				HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
                led_counter = 0;
			}
            uart_rx_buffer[0] = STOP;
        }	
        
        HAL_UART_Receive_IT(&huart2, uart_rx_buffer, sizeof(uart_rx_buffer));
    }
}