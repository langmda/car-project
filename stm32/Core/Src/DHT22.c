#include "dht22.h"
#include "delay.h"
#include <string.h>

// 复位DHT22
void DHT22_Rst(void)
{
    DHT22_Mode(OUT);
    DHT22_Low;
    delay_ms(10);       // 拉低10ms（典型值）
    DHT22_High;
    delay_us(40);       // 拉高40us
}

// 检测应答信号（超时200）
u8 DHT22_Check(void)
{
    u8 retry = 0;
    DHT22_Mode(IN);
    
    while (HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) && retry < 200) {
        retry++;
        delay_us(1);
    }
    if (retry >= 200) return 1;
    
    retry = 0;
    while (!HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) && retry < 200) {
        retry++;
        delay_us(1);
    }
    if (retry >= 200) return 1;
    
    return 0;
}

// 读取一个位（无中断保护，由外层统一关中断）
u8 DHT22_Read_Bit(void)
{
    u8 retry = 0;
    u8 bit = 0;
    
    // 等待低电平
    while (HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) && retry < 200) {
        retry++;
        delay_us(1);
    }
    retry = 0;
    // 等待高电平
    while (!HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) && retry < 200) {
        retry++;
        delay_us(1);
    }
    // 延时40us后判决（位0高电平约26-28us，位1约70us）
    delay_us(40);
    if (HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN))
        bit = 1;
    else
        bit = 0;
    
    return bit;
}

// 读取一个字节
u8 DHT22_Read_Byte(void)
{
    u8 i, dat = 0;
    for (i = 0; i < 8; i++) {
        dat <<= 1;
        dat |= DHT22_Read_Bit();
    }
    return dat;
}

// 读取温湿度数据（带重试，全程关中断）
u8 DHT22_Read_Data(float *temp, float *humi)
{
    u8 buf[5];
    u8 i;
    u16 temp_raw, humi_raw;
    u8 retry_cnt = 0;
    
    *temp = 0.0f;
    *humi = 0.0f;
    
    while (retry_cnt < 3) {
        DHT22_Rst();
        if (DHT22_Check() != 0) {
            retry_cnt++;
            delay_ms(2);
            continue;
        }
        
        // 从应答成功到读取完5个字节，全程关闭中断，保证时序
        __disable_irq();
        for (i = 0; i < 5; i++) {
            buf[i] = DHT22_Read_Byte();
        }
        __enable_irq();
        
        // 调试原始数据（如需要，取消注释）
        /*
        char dbg[60];
        sprintf(dbg, "RAW: %02X %02X %02X %02X %02X\r\n", buf[0],buf[1],buf[2],buf[3],buf[4]);
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, strlen(dbg), 100);
        */
        
        // 校验和
        if ((buf[0] + buf[1] + buf[2] + buf[3]) == buf[4]) {
            humi_raw = (buf[0] << 8) | buf[1];
            temp_raw = (buf[2] << 8) | buf[3];
            if (temp_raw & 0x8000) {
                temp_raw &= 0x7FFF;
                *temp = -((float)temp_raw / 10.0f);
            } else {
                *temp = (float)temp_raw / 10.0f;
            }
            *humi = (float)humi_raw / 10.0f;
            return 0;
        } else {
            retry_cnt++;
            delay_ms(2);
        }
    }
    return 2;  // 重试后仍失败
}

// 初始化
u8 DHT22_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    DHT22_GPIO_CLK_ENABLE();
    GPIO_InitStruct.Pin = DHT22_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT22_GPIO_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET);
    DHT22_Rst();
    return DHT22_Check();
}

// 引脚模式控制
void DHT22_Mode(u8 mode)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    if (mode == OUT) {
        GPIO_InitStruct.Pin = DHT22_GPIO_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    } else {
        GPIO_InitStruct.Pin = DHT22_GPIO_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
    }
    HAL_GPIO_Init(DHT22_GPIO_PORT, &GPIO_InitStruct);
}

// 中值滤波读取（内部排序采用冒泡）
#define MAX_FILTER_SAMPLES  10   // 最大采样次数

u8 DHT22_Read_Data_Filtered(float *temp, float *humi, uint8_t samples)
{
    if (samples == 0 || samples > MAX_FILTER_SAMPLES) {
        *temp = 0.0f;
        *humi = 0.0f;
        return 3;   // 参数错误
    }

    float temp_arr[MAX_FILTER_SAMPLES];
    float humi_arr[MAX_FILTER_SAMPLES];
    uint8_t count = 0;
    uint8_t attempts = 0;
    uint8_t max_attempts = samples * 3;  // 避免无限等待

    // 收集有效的温湿度读数
    while (count < samples && attempts < max_attempts) {
        float t, h;
        u8 ret = DHT22_Read_Data(&t, &h);
        if (ret == 0) {   // 读取成功
            temp_arr[count] = t;
            humi_arr[count] = h;
            count++;
        }
        attempts++;
        if (attempts < max_attempts) {
            delay_ms(5);   // 等待5ms再试，避免忙等
        }
    }

    if (count < samples) {
        // 未收集够有效样本
        *temp = 0.0f;
        *humi = 0.0f;
        return 4;   // 样本不足
    }

    // 对温度数组排序（冒泡）
    for (uint8_t i = 0; i < samples - 1; i++) {
        for (uint8_t j = 0; j < samples - i - 1; j++) {
            if (temp_arr[j] > temp_arr[j + 1]) {
                float tmp = temp_arr[j];
                temp_arr[j] = temp_arr[j + 1];
                temp_arr[j + 1] = tmp;
            }
        }
    }
    // 对湿度数组排序
    for (uint8_t i = 0; i < samples - 1; i++) {
        for (uint8_t j = 0; j < samples - i - 1; j++) {
            if (humi_arr[j] > humi_arr[j + 1]) {
                float tmp = humi_arr[j];
                humi_arr[j] = humi_arr[j + 1];
                humi_arr[j + 1] = tmp;
            }
        }
    }

    // 取中值
    if (samples % 2 == 1) {
        *temp = temp_arr[samples / 2];
        *humi = humi_arr[samples / 2];
    } else {   // 偶数个取中间两个平均
        *temp = (temp_arr[samples / 2 - 1] + temp_arr[samples / 2]) / 2.0f;
        *humi = (humi_arr[samples / 2 - 1] + humi_arr[samples / 2]) / 2.0f;
    }
    return 0;
}
