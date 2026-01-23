#include "sensor.h"
#include "tim.h"
#include "dht11.h"
#include "usart.h"

RawData_t raw_data; 

uint16_t adc_buf[2];

uint8_t read_and_transmit = 0;

void Encoder_Read(void) {
	// 读取原始脉冲数
	raw_data.encoder_left_cnt = __HAL_TIM_GET_COUNTER(&htim2);
	raw_data.encoder_right_cnt = __HAL_TIM_GET_COUNTER(&htim4);
	// 清零计数器
	__HAL_TIM_SET_COUNTER(&htim2, 0);
	__HAL_TIM_SET_COUNTER(&htim4, 0);
}

void Sensor_Read_And_Transmit(void) 
{
    Encoder_Read(); //读取编码器数据
	
	raw_data.temp = 0;
	raw_data.humi = 0;
    //DHT11_Read_Data(&raw_data.temp, &raw_data.humi);    //读取温湿度
	
    raw_data.pi_adc = 3652; //读取电池1电压
    raw_data.motor_adc = 3811; //读取电池2电压
    //raw_data.pi_adc = adc_buf[0]; //读取电池1电压
    //raw_data.motor_adc = adc_buf[1]; //读取电池2电压

    HAL_UART_Transmit(&huart2, (uint8_t*)&raw_data, sizeof(RawData_t),100);
}