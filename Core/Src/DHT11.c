#include "dht11.h"
#include "delay.h"
#include "usart.h"  // 如果需要调试输出

// 复位DHT11
void DHT11_Rst(void)	   
{                 
	DHT11_Mode(OUT); 	// SET OUTPUT
	DHT11_Low; 	      // 拉低DQ
	delay_ms(20);    	// 主机拉低18~30ms
	DHT11_High; 			// DQ=1 
	delay_us(13);     	// 主机拉高10~35us
}

// 等待DHT11的回应
// 返回1:未检测到DHT11的存在
// 返回0:存在
u8 DHT11_Check(void) 	   
{   
	u8 retry=0;
	DHT11_Mode(IN);// SET INPUT	 
    
    // 改为HAL库读取引脚
    while (HAL_GPIO_ReadPin(DHT11_GPIO_PORT, DHT11_GPIO_PIN) && retry<100) // DHT11会拉低40~80us
	{
		retry++;
		delay_us(1);
	}
	 
	if(retry>=100) return 1;
	
	retry=0;
    
    // 改为HAL库读取引脚
    while (!HAL_GPIO_ReadPin(DHT11_GPIO_PORT, DHT11_GPIO_PIN) && retry<100) // DHT11拉低后会再次拉高40~80us
	{
		retry++;
		delay_us(1);
	}
	
	if(retry>=100) return 1;	    
	
	return 0;
}

// 从DHT11读取一个位
// 返回值：1/0
u8 DHT11_Read_Bit(void) 			 
{
 	u8 retry=0;
    
    // 改为HAL库读取引脚
	while(HAL_GPIO_ReadPin(DHT11_GPIO_PORT, DHT11_GPIO_PIN) && retry<100) // 等待变为低电平
	{
		retry++;
		delay_us(1);
	}
	
	retry=0;
    
    // 改为HAL库读取引脚
	while(!HAL_GPIO_ReadPin(DHT11_GPIO_PORT, DHT11_GPIO_PIN) && retry<100) // 等待变高电平
	{
		retry++;
		delay_us(1);
	}
	
	delay_us(40); // 等待40us
    
    // 改为HAL库读取引脚
	if(HAL_GPIO_ReadPin(DHT11_GPIO_PORT, DHT11_GPIO_PIN))
        return 1;
	else 
        return 0;		   
}

// 从DHT11读取一个字节
// 返回值：读到的数据
u8 DHT11_Read_Byte(void)    
{        
	u8 i, dat;
	dat=0;
	for (i=0; i<8; i++) 
	{
		dat <<= 1; 
		dat |= DHT11_Read_Bit();
	}						    
	return dat;
}

// 从DHT11读取一次数据
// temp:温度值(范围:0~50°)
// humi:湿度值(范围:20%~90%)
// 返回值：0,正常;1,读取失败
u8 DHT11_Read_Data(u8 *temp, u8 *humi)    
{        
 	u8 buf[5];
	u8 i;
	
	*temp = 0;
    *humi = 0;
	
	DHT11_Rst();
	if(DHT11_Check() == 0)
	{
		for(i=0; i<5; i++) // 读取40位数据
		{
			buf[i] = DHT11_Read_Byte();
            
            // 调试输出原始数据
            /*
            char debug[50];
            sprintf(debug, "Byte%d: 0x%02X\r\n", i, buf[i]);
            HAL_UART_Transmit(&huart2, (uint8_t*)debug, strlen(debug), 100);
            */
		}
        
        // 调试输出所有数据
        /*
        char debug[100];
        sprintf(debug, "Raw: %d %d %d %d %d\r\n", buf[0], buf[1], buf[2], buf[3], buf[4]);
        HAL_UART_Transmit(&huart2, (uint8_t*)debug, strlen(debug), 100);
        */
        
		if((buf[0] + buf[1] + buf[2] + buf[3]) == buf[4])
		{
			*humi = buf[0];
			*temp = buf[2];
            return 0;
		}
        else
        {
            // 校验和错误
            return 2;
        }
	}
	else 
        return 1;
}

// 初始化DHT11的IO口 DQ 同时检测DHT11的存在
// 返回1:不存在
// 返回0:存在    	 	
u8 DHT11_Init(void)
{	 
 	GPIO_InitTypeDef GPIO_InitStructure;	
    
    // 使能GPIO时钟
    DHT11_GPIO_CLK_ENABLE();
    
 	GPIO_InitStructure.Pin = DHT11_GPIO_PIN;
 	GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP; 	// 推挽输出
 	GPIO_InitStructure.Pull = GPIO_PULLUP;          // 上拉
 	GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
 	HAL_GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStructure);
 	
    HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_GPIO_PIN, GPIO_PIN_SET); // 输出高
			    
	DHT11_Rst();  // 复位DHT11
	return DHT11_Check(); // 等待DHT11的回应
} 

// DHT11引脚输出模式控制
void DHT11_Mode(u8 mode)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	if(mode == OUT) // 输出模式
	{
		GPIO_InitStructure.Pin = DHT11_GPIO_PIN;
		GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;  // 推挽输出
		GPIO_InitStructure.Pull = GPIO_PULLUP;          // 上拉
		GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
	}
	else // 输入模式
	{
		GPIO_InitStructure.Pin = DHT11_GPIO_PIN;
		GPIO_InitStructure.Mode = GPIO_MODE_INPUT;      // 输入
		GPIO_InitStructure.Pull = GPIO_PULLUP;          // 上拉
	}
	
	HAL_GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStructure);
}