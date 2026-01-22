#include "servo.h"
#include "tim.h"

void Set_Angle(int angle)
{
    // 1. 角度边界校验，防止超出舵机量程
    if (angle < 0)
    {
        angle = 0;
    }
    else if (angle > 180)
    {
        angle = 180;
    }
    
    // 2. 计算对应角度的PWM脉宽值（单位：μs），脉宽 = 1000μs（0°） + 角度 × (1000μs / 180°)
    uint32_t pwm_pulse = 500 + (angle * 2000 / 180);
    
    // 3. 设置TIM1_CH1的比较值（CCR1），更新PWM脉宽
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, pwm_pulse);
    
    // 添加短暂延时，让舵机稳定到目标角度
    HAL_Delay(200); 
}
