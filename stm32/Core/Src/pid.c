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
volatile float Kp = 90.0f;
volatile uint16_t PWM_Limit = 4320;    // PWM 输出限幅

// 外环（航向角环）
volatile float YAW_KP = 2.0f;          // 航向 P
float YAW_OUT_LIMIT = 100.0f; // 外环输出限幅（即目标差速上限）
/* ================================================== */

volatile int16_t encoder_left = 0, encoder_right = 0;
volatile int32_t total_encoder_left = 0;
volatile int32_t total_encoder_right = 0;

int16_t last_left = 0, last_right = 0;

/* ---------- 内环状态 ---------- */
float pid_error = 0.0f;          // 内环误差 = 目标差速 - 实际差速
float pid_output = 0.0f;         // 内环输出（PWM 补偿量）
int16_t pid_base_pwm = 0;

/* ---------- 外环状态 ---------- */
float yaw_target_diff = 0.0f;    			// 记录外环输出，用于传感器读取失败、数据异常时沿用上一次外环输出
static uint32_t last_yaw_time = 0;

static float yaw_target_angle = 0.0f;		//航向角度
static uint8_t yaw_target_set = 0;			//标志位，表示当前是否设置过航向角度。0表示没有设定过，1表示设定过

//调试
float g_last_yaw = 0;

static uint32_t last_pid_tick = 0;
#define PID_PERIOD_MS 20
/* ==================== 基础函数 ==================== */

void PI_Param_Clear(void)
{
    pid_error = 0.0f;
    pid_output = 0.0f;
    pid_base_pwm = 0;
    yaw_target_diff = 0.0f;
    encoder_left = 0;
    encoder_right = 0;
	
	last_left = 0;
    last_right = 0;
    last_pid_tick = HAL_GetTick() - PID_PERIOD_MS;
	
	__HAL_TIM_SET_COUNTER(&htim2, 0);
	__HAL_TIM_SET_COUNTER(&htim4, 0);
}

//读取编码器，如果编码器读数异常大，将保留上次读取的数据
void Read_Encoder(void)
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
}

/* ==================== 外环：航向角环 ==================== */
/**
 * 读取 MPU6050，计算航向偏差，输出"目标左右轮差速"
 * 返回：目标差速（右轮 - 左轮），作为内环输入
 */
float Yaw_Loop_Update()
{
	float kp_local, yawkp_local;
	uint16_t pwm_lim_local;

	__disable_irq();
	yawkp_local  = YAW_KP;
	__enable_irq();
		
    float pitch, roll, current_yaw;

    uint32_t now = HAL_GetTick();

    if (mpu_dmp_get_data(&pitch, &roll, &current_yaw) != 0)		//读取失败，返回上次的外环输出
        return yaw_target_diff;
	g_last_yaw = current_yaw;

    if (current_yaw < 0) current_yaw += 360.0f;			//角度归一化到0到360度，其实这行没有也行，后面对角度误差归一化了

    if (!yaw_target_set) {					//还没有设定过航向角度，就将当前角度设定为航向角度
        yaw_target_angle = current_yaw;		
        yaw_target_set = 1;					//标记为已经设定过目标航向
    }

    float yaw_error = current_yaw - yaw_target_angle;
    if (yaw_error >  180.0f) yaw_error -= 360.0f;			//将角度误差归一化到-180到180度
    if (yaw_error < -180.0f) yaw_error += 360.0f;

    float yaw_target = -yaw_error * yawkp_local;
    if (yaw_target >  YAW_OUT_LIMIT) yaw_target =  YAW_OUT_LIMIT;
    if (yaw_target < -YAW_OUT_LIMIT) yaw_target = -YAW_OUT_LIMIT;

    yaw_target_diff = yaw_target;							//将临时外环输出存到全局变量
    return yaw_target_diff;
}

/* ==================== 内环：编码器速度差环 ==================== */
/**
 * 目标：让 (encoder_right - encoder_left) 跟踪 yaw_target_diff
 * 输出：PWM 补偿量
 */
float Speed_Loop_Update(float target_diff)
{
	float kp_local, yawkp_local;
	uint16_t pwm_lim_local;

	__disable_irq();
	kp_local     = Kp;
	pwm_lim_local = PWM_Limit;
	__enable_irq();
	
    float actual_diff = (float)(encoder_right - encoder_left);
    pid_error = target_diff - actual_diff;   // 内环误差

    pid_output = pid_error * kp_local;

    // 输出限幅
    if (pid_output >  pwm_lim_local) pid_output =  pwm_lim_local;
    if (pid_output < -pwm_lim_local) pid_output = -pwm_lim_local;

    return pid_output;
}

/* ==================== 主控制非阻塞函数 ==================== */
void PID_Go_Straight(int16_t base_pwm)			//由全局变量yaw_target_angle控制直行方向
{
	uint32_t now = HAL_GetTick();
    if (now - last_pid_tick < PID_PERIOD_MS) return;   // 唯一节流点
    last_pid_tick = now;
	
    pid_base_pwm = base_pwm;

    /* ---- 外环：航向 → 目标差速 ---- */
    Read_Encoder();                              
    float target_diff = Yaw_Loop_Update();       

    /* ---- 内环：跟踪目标差速 → PWM 补偿 ---- */
    float compensation = Speed_Loop_Update(target_diff);

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
	
	static uint32_t last_dbg = 0;
    if (HAL_GetTick() - last_dbg >= 200) {
        last_dbg = HAL_GetTick();
        char dbg[160];
        sprintf(dbg, "$[PID] tgt=%.1f yaw=%.1f diff=%.1f comp=%.0f L=%d R=%d*\r\n",
                yaw_target_angle,
                g_last_yaw,      // 如果 pid.c 里没有这个变量，就调 Yaw_Loop_Update 里那个
                target_diff,
                compensation,
                left_pwm, right_pwm);
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, strlen(dbg), 100);
    }
}

//避障中转向后调用，清除记录的外环输出，刷新航向角度
void PID_Set_Yaw_Target(float target)
{
    yaw_target_angle = target;
    yaw_target_set = 1;
    yaw_target_diff = 0.0f;
}
//避障中修正角度后调用，刷新航向角度，不清除记录的外环输出
void PID_Update_Yaw_Target(float target)
{
    yaw_target_angle = target;
    yaw_target_set = 1;
}
//小车停车时调用，清除记录的外环输出，不刷新航向角度
void PID_Reset_Yaw_Reference(void)
{
    yaw_target_set = 0;
    yaw_target_diff = 0.0f;
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