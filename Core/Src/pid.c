#include "pid.h"
#include "motor.h"
#include "tim.h" 
#include "stdio.h"
#include "usart.h"
#include "string.h"
#include <stdarg.h>

float Kp = 0.01f;
float Ki = 0.001f;
float I_Filtering = 0.85f;	//积分低通滤波系数
uint16_t I_Limit = 1000;		
uint16_t PWM_Limit = 4320; 

#define HARDWARE_RATIO_LOW 0.0f   // 动态计算的硬件补偿值系数，低速PWM20-30，系数稍大（0.07）
#define HARDWARE_RATIO_MID 0.0f  	 // 中速PWM30-40，你的最优值（0.067）
#define HARDWARE_RATIO_HIGH 0.0f 	 // 高速PWM40-50，系数稍小（0.065）
volatile int8_t dynamic_hw_fix = 0; // 动态计算的硬件补偿值，随基础PWM变化

volatile int16_t encoder_left = 0, encoder_right = 0;
uint32_t last_pid_time = 0;		

float pid_error = 0.0f;             
float pid_integral = 0.0f;          
float pid_output = 0.0f;           
int16_t pid_base_pwm = 0;          

void PI_Param_Clear(void)
{
    pid_error = 0.0f;
    pid_integral = 0.0f;
    pid_output = 0.0f;
    pid_base_pwm = 0;
	encoder_left = 0; 
    encoder_right = 0;   
	dynamic_hw_fix = 0;
}

void Read_Encoder(void)
{
    uint32_t current_time = HAL_GetTick();
    uint32_t time_diff = current_time - last_pid_time;
    
    if (time_diff >= 20) 	
    {  
        int16_t left = __HAL_TIM_GET_COUNTER(&htim4);
        int16_t right = __HAL_TIM_GET_COUNTER(&htim2);
        __HAL_TIM_SET_COUNTER(&htim2, 0);
        __HAL_TIM_SET_COUNTER(&htim4, 0);
		
        last_pid_time = current_time;
    }
}

float PID_Calculate(void)
{
	Read_Encoder();
	
    pid_error = (float)(encoder_right - encoder_left);
    
    float i_increment = pid_error * Ki;
	
    float temp_output = (pid_error * Kp) + pid_integral + i_increment;
	
    pid_integral = I_Filtering * pid_integral + (1 - I_Filtering) * i_increment;
	
    if(pid_integral > I_Limit) pid_integral = I_Limit;
    if(pid_integral < -I_Limit) pid_integral = -I_Limit;
    
    pid_output = (pid_error * Kp) + pid_integral;
	
	Print_Debug();
	
    return pid_output;
}

void PID_Go_Straight(int16_t base_pwm)
{
    pid_base_pwm = base_pwm;
    
    float compensation = PID_Calculate();
  
	 // 新增：实时计算动态硬件补偿值（核心！比例式，适配全PWM段）
    if(abs(pid_base_pwm) < 2160)		//0-30%
		dynamic_hw_fix = (int8_t)(abs(pid_base_pwm) * HARDWARE_RATIO_LOW);
	else if(abs(pid_base_pwm) < 2880)	//30%-40%
		dynamic_hw_fix = (int8_t)(abs(pid_base_pwm) * HARDWARE_RATIO_MID);
	else								//40%-50%
		dynamic_hw_fix = (int8_t)(abs(pid_base_pwm) * HARDWARE_RATIO_HIGH);
	
    int16_t left_pwm = pid_base_pwm + compensation - dynamic_hw_fix;
    int16_t right_pwm = pid_base_pwm - compensation + dynamic_hw_fix;

    left_pwm = (left_pwm > PWM_Limit) ? PWM_Limit : (left_pwm < -PWM_Limit ? -PWM_Limit : left_pwm);
    right_pwm = (right_pwm > PWM_Limit) ? PWM_Limit : (right_pwm < -PWM_Limit ? -PWM_Limit : right_pwm);

    Set_Motor(left_pwm, right_pwm);
}

void Debug_Print(const char* format, ...) 
{
    char buffer[128];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if(len > 0) 
	{
        HAL_UART_Transmit(&huart2, (uint8_t*)buffer, len, HAL_MAX_DELAY);
    }
}

// 简化的速度打印函数
void Print_Debug(void) 
{
    static uint32_t last_print = 0;
    uint32_t now = HAL_GetTick();
    
    if(now - last_print >= 100) 
	{
        // 打印：左计数、右计数、计数差、PID输出、积分、硬件补偿
        Debug_Print("L:%ld, R:%ld, Err:%.2f, Out:%.2f, I:%.2f, Fix:%d\r\n , Kp:%f\r\n",
                   encoder_left, encoder_right, pid_error, pid_output, 
                   pid_integral, dynamic_hw_fix,Kp);
        last_print = now;
    }
}