#include "pid.h"
#include "motor.h"
#include "tim.h" 
#include "stdio.h"
#include "usart.h"
#include "string.h"
#include <stdarg.h>
#include "inv_mpu.h"
float Kp = 10.0f;
float Ki = 1.0f;
uint16_t I_Limit = 100;		
uint16_t PWM_Limit = 4320; 

volatile int16_t encoder_left = 0, encoder_right = 0;
volatile int32_t total_encoder_left = 0;
volatile int32_t total_encoder_right = 0;

uint32_t last_pid_time = 0;		
int16_t last_left=0, last_right=0;

float pid_error = 0.0f;             
float pid_integral = 0.0f;          
float pid_output = 0.0f;           
int16_t pid_base_pwm = 0;       

static int16_t last_base_pwm_sign = 0;

static float start_yaw = 0.0f;
static uint8_t yaw_initialized = 0;
static uint32_t last_yaw_time = 0;

static uint8_t debug_cnt = 0;

void PI_Param_Clear(void)
{
    pid_error = 0.0f;
    pid_integral = 0.0f;
    pid_output = 0.0f;
    pid_base_pwm = 0;
	encoder_left = 0; 
    encoder_right = 0;   
}

void Read_Encoder(void)
{
    uint32_t current_time = HAL_GetTick();
    uint32_t time_diff = current_time - last_pid_time;
    
    if (time_diff >= 20) 	
    {  
		int16_t left = __HAL_TIM_GET_COUNTER(&htim4);
        int16_t right = __HAL_TIM_GET_COUNTER(&htim2);
        
		// 过滤异常跳变（超过 300 认为是干扰）
		if (abs(left - last_left) > 200) left = last_left;
		if (abs(right - last_right) > 200) right = last_right;
		last_left = left; 
		last_right = right;
        
        encoder_left = left;
        encoder_right = right;
		
		// 累加到全局累计变量
        total_encoder_left += left;
        total_encoder_right += right;
        
        __HAL_TIM_SET_COUNTER(&htim2, 0);
        __HAL_TIM_SET_COUNTER(&htim4, 0);
        
        last_pid_time = current_time;
    }
}

float PID_Calculate(void)
{
    static uint32_t last_time = 0;
    uint32_t now = HAL_GetTick();
    float dt = (now - last_time) / 1000.0f;  // 计算时间差（秒）
    if (dt <= 0.001f || dt > 0.1f) dt = 0.02f; // 默认20ms
    last_time = now;

    Read_Encoder();   // 更新 encoder_left, encoder_right
    pid_error = (float)(encoder_right - encoder_left);

    
	static uint8_t is_moving = 0;	//是否在移动
	if (abs(pid_base_pwm) > 100) is_moving = 1;
	else is_moving = 0;

	// 误差小于50个脉冲时启用积分，只在运动时累积积分
	if (abs(pid_error) < 50 && is_moving) 
	{
		pid_integral += pid_error * Ki * dt;
	}

    // 积分限幅
    if (pid_integral > I_Limit) pid_integral = I_Limit;
    if (pid_integral < -I_Limit) pid_integral = -I_Limit;

    // 比例+积分输出
    pid_output = (pid_error * Kp) + pid_integral;

    // 可选：输出限幅（防止超调）
    if (pid_output > PWM_Limit) pid_output = PWM_Limit;
    if (pid_output < -PWM_Limit) pid_output = -PWM_Limit;

    // 调试打印（取消注释即可）
    Print_Debug();

    return pid_output;
}

void PID_Go_Straight(int16_t base_pwm)
{
    pid_base_pwm = base_pwm;
    float compensation = PID_Calculate();
    
    float yaw_compensation = 0.0f;
    float pitch, roll, current_yaw;
    
    uint32_t now = HAL_GetTick();
    if (now - last_yaw_time >= 20) {
        last_yaw_time = now;
        if (mpu_dmp_get_data(&pitch, &roll, &current_yaw) == 0) 
		{
            if (current_yaw < 0) current_yaw += 360.0f;
            if (!yaw_initialized) {
                start_yaw = current_yaw;
                yaw_initialized = 1;
            }
            float yaw_error = current_yaw - start_yaw;
            if (yaw_error > 180.0f) yaw_error -= 360.0f;
            if (yaw_error < -180.0f) yaw_error += 360.0f;
            
            const float YAW_KP = 15.0f;
            yaw_compensation = yaw_error * YAW_KP;
            if (yaw_compensation > 200.0f) yaw_compensation = 200.0f;
            if (yaw_compensation < -200.0f) yaw_compensation = -200.0f;
			
			if (++debug_cnt >= 10) {
				debug_cnt = 0;
				char dbg_buf[128];
				sprintf(dbg_buf, "[ANGLE] yaw=%.1f start=%.1f err=%.2f comp=%.1f\r\n",
						current_yaw, start_yaw, yaw_error, yaw_compensation);
				HAL_UART_Transmit(&huart2, (uint8_t*)dbg_buf, strlen(dbg_buf), 100);
			}
        }
    }
    
    int16_t left_pwm, right_pwm;
    if (base_pwm >= 0) {
        left_pwm = base_pwm + (int16_t)compensation - (int16_t)yaw_compensation;
        right_pwm = base_pwm - (int16_t)compensation + (int16_t)yaw_compensation;
    } else {
        left_pwm = base_pwm - (int16_t)compensation + (int16_t)yaw_compensation;
        right_pwm = base_pwm + (int16_t)compensation - (int16_t)yaw_compensation;
    }
    
    left_pwm = (left_pwm > PWM_Limit) ? PWM_Limit : (left_pwm < -PWM_Limit) ? -PWM_Limit : left_pwm;
    right_pwm = (right_pwm > PWM_Limit) ? PWM_Limit : (right_pwm < -PWM_Limit) ? -PWM_Limit : right_pwm;
    
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
        Debug_Print("L:%ld, R:%ld, Err:%.2f, Out:%.2f, I:%.2f, Kp:%f\r\n",
                   encoder_left, encoder_right, pid_error, pid_output, 
                   pid_integral, Kp);
        last_print = now;
    }
}

void PID_Reset_Yaw_Reference(void)
{
    yaw_initialized = 0;
}

static uint8_t go_straight_continuous = 0;  // 持续直行标志

// 非阻塞直行（每周期调用一次，持续前进）
void PID_Go_Straight_Continuous(int16_t base_pwm)
{
    pid_base_pwm = base_pwm;
    float compensation = PID_Calculate();   // 计算补偿
    int16_t left_pwm, right_pwm;
    if (base_pwm >= 0) {
        left_pwm  = base_pwm + (int16_t)compensation;
        right_pwm = base_pwm - (int16_t)compensation;
    } else {
        left_pwm  = base_pwm - (int16_t)compensation;
        right_pwm = base_pwm + (int16_t)compensation;
    }
    // 限幅
    left_pwm  = (left_pwm  > PWM_Limit) ? PWM_Limit : (left_pwm  < -PWM_Limit) ? -PWM_Limit : left_pwm;
    right_pwm = (right_pwm > PWM_Limit) ? PWM_Limit : (right_pwm < -PWM_Limit) ? -PWM_Limit : right_pwm;
    Set_Motor(left_pwm, right_pwm);
}

// 启动持续直行
void PID_Start_Continuous(int16_t base_pwm)
{
    go_straight_continuous = 1;
    // 可选：重置编码器累计值，避免误差累积
    total_encoder_left = 0;
    total_encoder_right = 0;
    // 设置基础速度
    pid_base_pwm = base_pwm;
    // 清空 PID 积分项（可选）
    pid_integral = 0;
}

// 停止持续直行
void PID_Stop_Continuous(void)
{
    go_straight_continuous = 0;
    Set_Motor(0, 0);
}