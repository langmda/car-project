#include "delay.h"

static uint32_t usTicks = 0;

// 初始化DWT
void delay_init(void)
{
    // 使能DWT
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    
    // 计算每微秒的时钟周期数
    usTicks = HAL_RCC_GetHCLKFreq() / 1000000;
}

// 微秒延时
void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * usTicks;
    
    // 等待经过指定的时钟周期数
    while ((DWT->CYCCNT - start) < ticks);
}

// 毫秒延时（直接使用HAL_Delay）
void delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}