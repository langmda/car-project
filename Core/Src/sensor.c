#include "sensor.h"
#include "tim.h"
#include "dht11.h"
#include "usart.h"
#include "pid.h"
#include "user.h"
#include "inv_mpu.h"

RawData_t raw_data; 

uint16_t adc_buf[2];

void Read_And_Transmit(void) 
{
	raw_data.cmd = SENSOR_DATA; 
	
	raw_data.encoder_left = encoder_left;
	raw_data.encoder_right = encoder_right;
	
	DHT11_Read_Data(&raw_data.temp, &raw_data.humi);
	
    raw_data.pi_adc = adc_buf[0]; //读取电池1电压
    raw_data.motor_adc = adc_buf[1]; //读取电池2电压
	
    float pitch, roll;
    if (mpu_dmp_get_data(&pitch, &roll, &raw_data.yaw) == 0) 
	{
        if (raw_data.yaw < 0) 
		{
			raw_data.yaw += 360.0f;
		}
    } 
	else 
	{
        raw_data.yaw = 0.0f;   // 读取失败，赋默认值
    }

    HAL_UART_Transmit(&huart2, (uint8_t*)&raw_data, sizeof(RawData_t),100);
}