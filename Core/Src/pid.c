#include "pid.h"
#include "motor.h"
#include "tim.h"
#include "stdio.h"
#include "usart.h"
#include "string.h"
#include <stdarg.h>
#include "inv_mpu.h"

/* ==================== 可调参数 ==================== */
// 内环（编码器速度差环）
float Kp = 90.0f;
float Ki = 0.8f;
uint16_t I_Limit   = 800;     // 积分限幅
uint16_t PWM_Limit = 4320;    // PWM 输出限幅
float DIFF_LIMIT = 1200.0f;   // 内环目标差速限幅（脉冲/周期）

// 外环（航向角环）
float YAW_KP = 2.0f;          // 航向 P
float YAW_OUT_LIMIT = 800.0f; // 外环输出限幅（即目标差速上限）
/* ================================================== */

volatile int16_t encoder_left = 0, encoder_right = 0;
volatile int32_t total_encoder_left = 0;
volatile int32_t total_encoder_right = 0;

uint32_t last_pid_time = 0;
int16_t last_left = 0, last_right = 0;

/* ---------- 内环状态 ---------- */
float pid_error = 0.0f;          // 内环误差 = 目标差速 - 实际差速
float pid_integral = 0.0f;
float pid_output = 0.0f;         // 内环输出（PWM 补偿量）
int16_t pid_base_pwm = 0;

/* ---------- 外环状态 ---------- */
float yaw_target_diff = 0.0f;    // 外环输出：目标差速
static float start_yaw = 0.0f;
static uint8_t yaw_initialized = 0;
static uint32_t last_yaw_time = 0;

static uint8_t debug_cnt = 0;

/* ==================== 基础函数 ==================== */

void PI_Param_Clear(void)
{
    pid_error = 0.0f;
    pid_integral = 0.0f;
    pid_output = 0.0f;
    pid_base_pwm = 0;
    yaw_target_diff = 0.0f;
    encoder_left = 0;
    encoder_right = 0;
	
	last_left = 0;
    last_right = 0;
    last_pid_time = HAL_GetTick();
}

void Read_Encoder(void)
{
    uint32_t current_time = HAL_GetTick();
    uint32_t time_diff = current_time - last_pid_time;

    if (time_diff >= 20)
    {
        int16_t left  = __HAL_TIM_GET_COUNTER(&htim4);
        int16_t right = __HAL_TIM_GET_COUNTER(&htim2);

        // 软滤波：异常跳变
        if (abs((int)left  - last_left)  > 200) left  = last_left;
        if (abs((int)right - last_right) > 200) right = last_right;
        last_left  = left;
        last_right = right;

        encoder_left  = left;
        encoder_right = right;

        total_encoder_left  += left;
        total_encoder_right += right;

        __HAL_TIM_SET_COUNTER(&htim2, 0);
        __HAL_TIM_SET_COUNTER(&htim4, 0);

        last_pid_time = current_time;
    }
}

/* ==================== 外环：航向角环 ==================== */
/**
 * 读取 MPU6050，计算航向偏差，输出"目标左右轮差速"
 * 返回：目标差速（右轮 - 左轮），正数表示需要右转补偿
 */
float Yaw_Loop_Update(float dt)
{
    float yaw_target = 0.0f;
    float pitch, roll, current_yaw;

    uint32_t now = HAL_GetTick();
    if (now - last_yaw_time < 20) return yaw_target_diff;  // 未到周期，返回上次值
    last_yaw_time = now;

    if (mpu_dmp_get_data(&pitch, &roll, &current_yaw) != 0)
        return yaw_target_diff;  // 读取失败，保持上次输出

    if (current_yaw < 0) current_yaw += 360.0f;

    if (!yaw_initialized) {
        start_yaw = current_yaw;
        yaw_initialized = 1;
    }

    float yaw_error = current_yaw - start_yaw;
    // 归一化到 [-180, 180]
    if (yaw_error >  180.0f) yaw_error -= 360.0f;
    if (yaw_error < -180.0f) yaw_error += 360.0f;

    // 外环 P（可扩展 I）
    yaw_target = -yaw_error * YAW_KP;

    // 外环输出限幅：限制目标差速
    if (yaw_target >  YAW_OUT_LIMIT) yaw_target =  YAW_OUT_LIMIT;
    if (yaw_target < -YAW_OUT_LIMIT) yaw_target = -YAW_OUT_LIMIT;

    yaw_target_diff = yaw_target;

    // 调试打印
    if (++debug_cnt >= 10) {
        debug_cnt = 0;
        char dbg_buf[128];
        sprintf(dbg_buf, "[YAW] yaw=%.1f start=%.1f err=%.2f target=%.1f\r\n",
                current_yaw, start_yaw, yaw_error, yaw_target_diff);
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg_buf, strlen(dbg_buf), 100);
    }

    return yaw_target_diff;
}

/* ==================== 内环：编码器速度差环 ==================== */
/**
 * 目标：让 (encoder_right - encoder_left) 跟踪 yaw_target_diff
 * 输出：PWM 补偿量
 */
float Speed_Loop_Update(float target_diff, float dt)
{
    Read_Encoder();

    float actual_diff = (float)(encoder_right - encoder_left);
    pid_error = target_diff - actual_diff;   // 注意：目标 - 实际

    // 积分分离 + 运动判断
    static uint8_t is_moving = 0;
    is_moving = (abs(pid_base_pwm) > 100) ? 1 : 0;

    if (abs(pid_error) < 80.0f && is_moving) {
        pid_integral += pid_error * Ki * dt;
    }

    // 积分限幅
    if (pid_integral >  I_Limit) pid_integral =  I_Limit;
    if (pid_integral < -I_Limit) pid_integral = -I_Limit;

    // PI 输出
    pid_output = pid_error * Kp + pid_integral;

    // 输出限幅
    if (pid_output >  PWM_Limit) pid_output =  PWM_Limit;
    if (pid_output < -PWM_Limit) pid_output = -PWM_Limit;

    return pid_output;
}

/* ==================== 主控制函数（串级） ==================== */
void PID_Go_Straight(int16_t base_pwm)
{
    static uint32_t last_time = 0;
    uint32_t now = HAL_GetTick();
    float dt = (now - last_time) / 1000.0f;
    if (dt <= 0.001f || dt > 0.1f) dt = 0.02f;
    last_time = now;

    pid_base_pwm = base_pwm;

    /* ---- 外环：航向 → 目标差速 ---- */
    float target_diff = Yaw_Loop_Update(dt);

    /* ---- 内环：跟踪目标差速 → PWM 补偿 ---- */
    float compensation = Speed_Loop_Update(target_diff, dt);

    /* ---- 叠加到 PWM ---- */
    int16_t left_pwm, right_pwm;
    if (base_pwm >= 0) {
        // 目标差速为正 → 右轮加速、左轮减速
        left_pwm  = base_pwm - (int16_t)compensation;
        right_pwm = base_pwm + (int16_t)compensation;
    } else {
        // 倒车时方向反过来
        left_pwm  = base_pwm + (int16_t)compensation;
        right_pwm = base_pwm - (int16_t)compensation;
    }

    // 限幅
    if (left_pwm  >  PWM_Limit) left_pwm  =  PWM_Limit;
    if (left_pwm  < -PWM_Limit) left_pwm  = -PWM_Limit;
    if (right_pwm >  PWM_Limit) right_pwm =  PWM_Limit;
    if (right_pwm < -PWM_Limit) right_pwm = -PWM_Limit;

    Set_Motor(left_pwm, right_pwm);
	Print_Debug();
}

/* ==================== 只用编码器直行（无陀螺仪） ==================== */
void PID_Go_Straight_EncoderOnly(int16_t base_pwm)
{
    static uint32_t last_time = 0;
    uint32_t now = HAL_GetTick();
    float dt = (now - last_time) / 1000.0f;
    if (dt <= 0.001f || dt > 0.1f) dt = 0.02f;
    last_time = now;

    pid_base_pwm = base_pwm;

    // 目标差速 = 0，纯走直线
    float compensation = Speed_Loop_Update(0.0f, dt);

    int16_t left_pwm, right_pwm;
    if (base_pwm >= 0) {
        left_pwm  = base_pwm - (int16_t)compensation;
        right_pwm = base_pwm + (int16_t)compensation;
    } else {
        left_pwm  = base_pwm + (int16_t)compensation;
        right_pwm = base_pwm - (int16_t)compensation;
    }

    if (left_pwm  >  PWM_Limit) left_pwm  =  PWM_Limit;
    if (left_pwm  < -PWM_Limit) left_pwm  = -PWM_Limit;
    if (right_pwm >  PWM_Limit) right_pwm =  PWM_Limit;
    if (right_pwm < -PWM_Limit) right_pwm = -PWM_Limit;

    Set_Motor(left_pwm, right_pwm);
}

/* ==================== 调试 ==================== */
void Debug_Print(const char* format, ...)
{
    char buffer[128];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (len > 0) {
        HAL_UART_Transmit(&huart2, (uint8_t*)buffer, len, HAL_MAX_DELAY);
    }
}

void Print_Debug(void)
{
    static uint32_t last_print = 0;
    uint32_t now = HAL_GetTick();
    if (now - last_print >= 100) {
        Debug_Print(DBG_HEAD "L:%d R:%d" DBG_TAIL,
                    encoder_left, encoder_right);
        last_print = now;
    }
}

void PID_Reset_Yaw_Reference(void)
{
    yaw_initialized = 0;
    pid_integral = 0.0f;        // 重置航向时顺便清积分
    yaw_target_diff = 0.0f;
}