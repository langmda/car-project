#include "uart.h"
#include "usart.h"

//模式
#define AUTOMATIC  0x01		//自主避障模式
#define MANUAL     0x02		//手动操作模式

//命令
#define MODE_SWITCH 0X05	//模式切换
#define LED_SWITCH 0x06     //灯光切换

uint8_t led_counter = 0;    //led计数器
uint8_t uart_rx_buffer[1] = {0x55};
uint8_t mode = MANUAL;		//默认模式为手动操作

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        if(uart_rx_buffer[0] == MODE_SWITCH)   //模式切换
        {
            if(mode == MANUAL)  {mode = AUTOMATIC;}
            else                {mode = MANUAL;}
            //uart_rx_buffer[0] = 0x00;
        }
        if(uart_rx_buffer[0] == LED_SWITCH)    //灯光切换
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
                led_counter = 0;
            }
			//uart_rx_buffer[0] = 0x00;
        }
        HAL_UART_Receive_IT(&huart2, uart_rx_buffer, sizeof(uart_rx_buffer));
    }
}