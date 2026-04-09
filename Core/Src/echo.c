#include "echo.h"
#include "tim.h"
#include "servo.h"
#include "motor.h"
#include "sensor.h"
#include "delay.h"

uint32_t echo_start = 0;    // Echo上升沿的计数值（发出超声波）
uint32_t echo_end = 0;      // Echo下降沿的计数值（超声波返回）
uint32_t tim1_overflow = 0; // TIM1溢出次数（处理长脉冲）
float distance = 0.0f;      // 最终测距结果（单位：cm）
uint8_t capture_state = 0;  // 捕获状态：0=等待上升沿，1=等待下降沿
uint8_t echo_measure_done = 0;		//全局标志位：标记测距完成
uint8_t distance_lock = 0;		//对距离操作的状态锁
float left_distance;		
float right_distance;
float front_distance;
uint8_t sensor_counter = 0;	//用于舵机定时器的更新中断中（20ms），计数来实现每隔1s读取并发送传感器数据

//更新中断函数，用于记录测距过程中计数器溢出次数，来计算距离，并实现0.1s读取一次传感器数据
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1)
    {
        tim1_overflow++; // 记录测距过程中计数器溢出次数

        sensor_counter++;      //记录更新中断的次数，用于实现1s的更新中断
        if(sensor_counter >= 5) // 每过0.1s就读取传感器数据并发送，并计算车速
        {
            read_and_transmit = 1;
            sensor_counter = 0;
        }
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

// 多次测距并返回中位数（count：测距次数，建议奇数如3/5）
float Get_Median_Distance(uint8_t count)
{
    float distances[count]; // 存储多次测距结果
    uint8_t valid_cnt = 0;  // 有效数据计数
    uint8_t i, j;
    float temp;

    // 1. 多次测距，收集有效数据
    for(i = 0; i < count; i++)
    {
        Trig();
        Wait_Echo_Measure();
        while(distance_lock){}; // 等待解锁
        
        // 过滤无效值（超出量程）
        if(distance > 2.0f && distance < 400.0f)
        {
            distances[valid_cnt++] = distance;
        }
        
        HAL_Delay(60); // 两次测距间隔，避免声波干扰
    }

    // 2. 若无有效数据，返回0
    if(valid_cnt == 0) return 0.0f;

    // 3. 冒泡排序（升序）
    for(i = 0; i < valid_cnt - 1; i++)
    {
        for(j = 0; j < valid_cnt - i - 1; j++)
        {
            if(distances[j] > distances[j+1])
            {
                temp = distances[j];
                distances[j] = distances[j+1];
                distances[j+1] = temp;
            }
        }
    }

    // 4. 返回中位数（奇数个取中间，偶数个取中间两个平均）
    if(valid_cnt % 2 == 1)
    {
        return distances[valid_cnt / 2];
    }
    else
    {
        return (distances[valid_cnt/2 - 1] + distances[valid_cnt/2]) / 2.0f;
    }
}

//获取左右障碍物的距离
void Get_Left_Right_Distance()
{
    Set_Angle(30);		//测量右边
	right_distance = Get_Median_Distance(3);

	HAL_Delay(60);		//在两次测量中间添加延时，避免上一次的声波影响测量
		
    Set_Angle(150);		//测量左边
    left_distance = Get_Median_Distance(3);

    Set_Angle(90);
}