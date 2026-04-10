#include "sensor.h"
#include "tim.h"
#include "dht11.h"
#include "usart.h"
#include "pid.h"
#include "user.h"

RawData_t raw_data; 

uint16_t adc_buf[2];

uint8_t read_and_transmit = 0;		//读取并发送的标志位

void Read_And_Transmit(void) 
{
	raw_data.cmd = SENSOR_DATA; 
	
	raw_data.encoder_left = encoder_left;
	raw_data.encoder_right = encoder_right;
	
	DHT11_Read_Data(&raw_data.temp, &raw_data.humi);
	
    raw_data.pi_adc = adc_buf[0]; //读取电池1电压
    raw_data.motor_adc = adc_buf[1]; //读取电池2电压

    HAL_UART_Transmit(&huart2, (uint8_t*)&raw_data, sizeof(RawData_t),100);
}