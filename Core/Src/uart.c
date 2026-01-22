#include "uart.h"
#include "usart.h"
#include "user.h"

//模式
#define AUTOMATIC  0x01		//自主避障模式
#define MANUAL     0x02		//手动操作模式

//命令
#define STOP        0X00	//停止
#define MOVE_FORWARD 0X01	//前进
#define TURN_LEFT 0X02		//左转
#define TURN_RIGHT 0X03		//右转
#define BACK_OFF 0X04		//后退
#define MODE_SWITCH 0X05	//模式切换
#define LED_SWITCH 0x06     //灯光切换

uint8_t led_counter = 0;    //led计数器
uint8_t mode = MANUAL;		//默认模式为手动操作
uint8_t uart_rx_buffer[] = {0x00};

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
		if (uart_rx_buffer[0] == STOP || 
            uart_rx_buffer[0] == MOVE_FORWARD || 
            uart_rx_buffer[0] == BACK_OFF || 
            uart_rx_buffer[0] == TURN_LEFT || 
            uart_rx_buffer[0] == TURN_RIGHT)
        {
            current_cmd = uart_rx_buffer[0];	          // 用一个全局变量存“当前有效指令”
			last_cmd_time = HAL_GetTick();  // 更新时间
        }
        else if(uart_rx_buffer[0] == MODE_SWITCH)   //模式切换
        {
            if(mode == MANUAL)  {mode = AUTOMATIC;}
            else                {mode = MANUAL;}
            uart_rx_buffer[0] = 0x00;
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
                led_counter = 0;
            }
			uart_rx_buffer[0] = 0x00;
        }
        HAL_UART_Receive_IT(&huart2, uart_rx_buffer, sizeof(uart_rx_buffer));
    }
}