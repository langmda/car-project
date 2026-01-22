#include "dht11.h"

// ====================== 仅保留这1个微秒延时函数 ======================
// 微秒延时函数（HAL库无原生us延时，手动实现，适配72MHz系统时钟）
void DHT11_DelayUs(u16 us)
{
    // 计算延时计数值：HCLK=72MHz → 1us=72个时钟周期，除以8减少循环次数
    uint32_t ticks = (HAL_RCC_GetHCLKFreq() / 1000000) * us / 8;
    while(ticks--);
}

// DHT11引脚设置为输出模式（推挽输出+上拉）
void DHT11_Set_Out(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    __HAL_RCC_GPIOB_CLK_ENABLE();  // 对应DHT11_PORT的时钟使能
    
    GPIO_InitStruct.Pin = DHT11_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  // 推挽输出
    GPIO_InitStruct.Pull = GPIO_PULLUP;          // 上拉（DHT11需要）
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; // 低速即可
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

// DHT11引脚设置为输入模式（上拉输入）
void DHT11_Set_In(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    __HAL_RCC_GPIOB_CLK_ENABLE();  // 对应DHT11_PORT的时钟使能
    
    GPIO_InitStruct.Pin = DHT11_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;     // 输入模式
    GPIO_InitStruct.Pull = GPIO_PULLUP;         // 上拉，避免浮空
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

// DHT11初始化（发送起始信号，等待响应）
u8 DHT11_Init(void)
{
    DHT11_Set_Out();                     // 先设为输出
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET); // 拉高标准
    HAL_Delay(100);                      // 等待DHT11上电稳定
    
    // 发送起始信号：拉低至少18ms
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
    DHT11_DelayUs(30);                  // 拉高20~40us
    
    // 切换为输入，等待DHT11响应
    DHT11_Set_In();
    while(HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET); // 等待DHT11拉低
    while(HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET); // 等待DHT11拉高
    while(HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET); // 等待DHT11拉低
    
    return 0; // 初始化成功
}

// 读取1字节数据（DHT11的位时序解析）
u8 DHT11_Read_Byte(void)
{
    u8 data = 0;
    u8 i;
    for(i=0; i<8; i++)
    {
        while(HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET); // 等待拉高
        DHT11_DelayUs(40); // 延时40us，判断是0（<40us）还是1（>40us）
        
        data <<= 1;
        if(HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
        {
            data |= 0x01; // 高电平=1
        }
        while(HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET); // 等待拉低
    }
    return data;
}

// 读取温湿度数据（核心函数）
// 返回值：0=成功，1=校验失败；temp=温度（整数），humi=湿度（整数）
u8 DHT11_Read_Data(u8 *temp, u8 *humi)
{
    u8 buf[5] = {0};
    u8 i;
    
    if(DHT11_Init() != 0)
    {
        return 1; // 初始化失败
    }
    
    // 读取40位数据（5字节：湿度整数、湿度小数、温度整数、温度小数、校验和）
    for(i=0; i<5; i++)
    {
        buf[i] = DHT11_Read_Byte();
    }
    
    // 校验：前4字节之和 = 第5字节
    if(buf[0] + buf[1] + buf[2] + buf[3] == buf[4])
    {
        *humi = buf[0];  // DHT11小数部分恒为0，只取整数
        *temp = buf[2];
        return 0;
    }
    else
    {
        return 1; // 校验失败，数据不可信
    }
}