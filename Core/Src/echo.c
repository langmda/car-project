#include "echo.h"
#include "tim.h"

uint32_t echo_start = 0;    // Echo上升沿的计数值（发出超声波）
uint32_t echo_end = 0;      // Echo下降沿的计数值（超声波返回）
uint32_t tim1_overflow = 0; // TIM1溢出次数（处理长脉冲）
float distance = 0.0f;      // 最终测距结果（单位：cm）
uint8_t capture_state = 0;  // 捕获状态：0=等待上升沿，1=等待下降沿
uint8_t echo_measure_done = 0;		//全局标志位：标记测距完成
uint8_t distance_lock = 0;		//对距离操作的状态锁

// 简易μs延时函数（基于系统时钟72MHz）
void delay_us(uint32_t us)
{
    uint32_t ticks = (HAL_RCC_GetHCLKFreq() / 1000000) * us;
    ticks /= 5; 
    while(ticks--);
}

//更新中断函数，用于记录测距过程中计数器溢出次数，来计算距离
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1)
    {
        tim1_overflow++; // 每溢出一次（20ms），计数+1
    }
}

//触发超声波传感器，距离在收到返回的超声波，触发输入捕获中断后获得
void Trig()
{
	echo_measure_done = 0; // 重置测距标志
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);
    delay_us(12);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
}

//输入捕获中断函数
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) 
{
    if (htim->Instance == TIM1 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) 
    {
		distance_lock = 1;	//上锁
        TIM_IC_InitTypeDef sConfigIC = {0};
        switch(capture_state)
        {
            case 0:     //捕获到上升沿（发出超声波）
                // 1. 记录上升沿的计数值
                echo_start = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
                // 2. 重置溢出计数
                tim1_overflow = 0;
                // 3. 切换捕获极性为下降沿，准备捕获结束值
                sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
                sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
                sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
                sConfigIC.ICFilter = 0;
                HAL_TIM_IC_ConfigChannel(&htim1, &sConfigIC, TIM_CHANNEL_2);
                // 4. 重启捕获中断（生效新极性）
                HAL_TIM_IC_Start_IT(&htim1, TIM_CHANNEL_2);
                // 5. 切换状态为等待下降沿
                capture_state = 1;
                break;
            case 1:     //捕获到下降沿（超声波返回）
                // 1. 记录下降沿的计数值
                echo_end = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
                // 2. 计算总时间（包含溢出次数）：溢出次数×20000μs + （结束值-起始值）
                uint32_t total_time_us = tim1_overflow * 20000 + (echo_end - echo_start);
                // 3. 换算距离：距离(cm) = 时间(μs) × 声速(0.0343cm/μs) / 2
                distance = (float)total_time_us * 0.0343f / 2.0f;
                // 4. 边界值过滤（避免无效数据，根据模块量程调整）
                if (distance < 2.0f || distance > 400.0f)
                {
                    distance = 0.0f; // 超出量程，置为0
                }
                // 5. 切换回捕获上升沿，准备下一次测量
                sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
                sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
                sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
                sConfigIC.ICFilter = 0;
                HAL_TIM_IC_ConfigChannel(&htim1, &sConfigIC, TIM_CHANNEL_2);
                HAL_TIM_IC_Start_IT(&htim1, TIM_CHANNEL_2);
                // 6. 重置状态和溢出计数
                capture_state = 0;
                tim1_overflow = 0;
				echo_measure_done = 1; // 测距完成，置位标志
                break;
        }
		distance_lock = 0; // 解锁
    }
}

// 等待测距完成的函数
void Wait_Echo_Measure()
{
    uint32_t timeout = 1000; // 超时1000ms则停止等待，避免死等
    while(!echo_measure_done && timeout--)
    {
        HAL_Delay(1);
    }
}

