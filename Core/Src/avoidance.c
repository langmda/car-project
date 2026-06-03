#include "avoidance.h"
#include "servo.h"
#include "echo.h"
#include "motor.h"
#include "trajectory.h"
#include "pid.h"
#include "usart.h"
#include "delay.h"
#include "stdio.h"
#include "user.h"
#include "stdbool.h"
#include "inv_mpu.h"

// ---------- 可调参数 ----------
#define FORWARD_SPEED           800     // 直行速度 PWM
#define FRONT_DIST_THRESH       15.0f   // 前方障碍阈值 (cm)
#define LEFT_RIGHT_DIST_THRESH  40.0f   // 左右方障碍阈值 (cm)
#define TURN_DELAY_MS           100     // 停车后等待转向的时间
#define YAW_KP                  -10.0f   // 直行时角度补偿系数
#define SCAN_DELAY_MS           60      // 舵机稳定时间

// 舵机角度
#define ANGLE_FRONT             90
#define ANGLE_LEFT              180
#define ANGLE_RIGHT             0

static volatile uint8_t maze_running = 1;
extern uint8_t mode;

static float target_yaw = 0.0f;   // 当前目标航向角（0~360）

static uint32_t last_yaw_print = 0;

// ---------- 辅助函数 ----------
// 获取当前 yaw（0~360）
float GetCurrentYaw(void)
{
    float pitch, roll, yaw;
    if (mpu_dmp_get_data(&pitch, &roll, &yaw) == 0) {
        if (yaw < 0) yaw += 360.0f;
        return yaw;
    }
    return -1.0f; // 错误
}

// 直行（带 yaw 闭环，非阻塞）
static void GoStraightWithYaw(int16_t base_pwm, float target)
{
    float now = GetCurrentYaw();
    if (now < 0) return;
    
    float error = now - target;
    if (error > 180.0f) error -= 360.0f;
    if (error < -180.0f) error += 360.0f;
    
    int16_t yaw_comp = (int16_t)(error * YAW_KP);
    int16_t left_pwm  = base_pwm - yaw_comp;
    int16_t right_pwm = base_pwm + yaw_comp;
    
    // 限幅
    if (left_pwm > 4320) left_pwm = 4320;
    if (left_pwm < -4320) left_pwm = -4320;
    if (right_pwm > 4320) right_pwm = 4320;
    if (right_pwm < -4320) right_pwm = -4320;
    
    Set_Motor(left_pwm, right_pwm);
    
    // 调试打印：每 500ms 打印一次（避免刷屏）
    uint32_t now_ms = HAL_GetTick();
    if (now_ms - last_yaw_print >= 500) {
        last_yaw_print = now_ms;
        char msg[80];
        sprintf(msg, "[STRAIGHT] yaw=%.1f target=%.1f err=%.1f comp=%d L=%d R=%d\r\n",
                now, target, error, yaw_comp, left_pwm, right_pwm);
        HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
    }
}

// 获取指定方向的距离（单次）
static float GetDistanceAt(int angle)
{
    Set_Angle(angle);
    HAL_Delay(SCAN_DELAY_MS);
    float dist = Get_Median_Distance(1);
    if (dist < 2.0f || dist > 400.0f) dist = 100.0f;
    return dist;
}

// 获取左右距离（阻塞）
static void GetLeftRightDistances(float *left, float *right)
{
    *left  = GetDistanceAt(ANGLE_LEFT);
    *right = GetDistanceAt(ANGLE_RIGHT);
    Set_Angle(ANGLE_FRONT);
}

// 转向到目标角度（基于绝对差值）
static void TurnToTarget(float new_target)
{
    float current = GetCurrentYaw();
    float delta = new_target - current;
    if (delta > 180.0f) delta -= 360.0f;
    if (delta < -180.0f) delta += 360.0f;
    
    char msg[64];
    sprintf(msg, "[TURN] Current=%.1f, target=%.1f, delta=%.1f\r\n", current, new_target, delta);
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
    
    turn_angle_blocking((int16_t)delta);
    target_yaw = new_target;
}

// ---------- 决策：右手法则 ----------
static int8_t DecideTurn(bool right_wall, bool front_wall, bool left_wall)
{
    if (!right_wall) return 1;   // 右转
    if (!front_wall) return 0;   // 直行
    if (!left_wall)  return -1;  // 左转
    return 2;                    // 掉头
}

// ---------- 主函数 ----------
void Maze_AbsoluteYaw(void)
{
    char msg[64];
    
    // 初始化
    Set_Angle(ANGLE_FRONT);
    HAL_Delay(200);
    for (int i = 0; i < 10; i++) GetCurrentYaw();   // 预热 DMP
    target_yaw = GetCurrentYaw();
    if (target_yaw < 0) target_yaw += 360.0f;
    
    sprintf(msg, "[MAZE] Start, target_yaw=%.1f\r\n", target_yaw);
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
    
    maze_running = 1;
    
    while (maze_running) {
        // ---------- 阶段1：直行直到前方有障碍 ----------
        uint32_t last_check = HAL_GetTick();
        while (maze_running) {
            GoStraightWithYaw(FORWARD_SPEED, target_yaw);
            uint32_t now = HAL_GetTick();
            if (now - last_check >= 100) {
                last_check = now;
                float front = GetDistanceAt(ANGLE_FRONT);
                sprintf(msg, "[FRONT] %.1f cm\r\n", front);
                HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
                if (front < FRONT_DIST_THRESH) break;
            }
            HAL_Delay(10);
        }
        if (!maze_running) break;
        
        // ---------- 阶段2：停车，测左右距离 ----------
        Set_Motor(0, 0);
        HAL_Delay(TURN_DELAY_MS);
        float left, right;
        GetLeftRightDistances(&left, &right);
        sprintf(msg, "[LR] Left=%.1f, Right=%.1f\r\n", left, right);
        HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
        
        // ---------- 阶段3：右手法则决策 ----------
        bool right_wall = (right < LEFT_RIGHT_DIST_THRESH);   // 复用前方阈值
        bool front_wall = true;   // 既然进入转向，前方肯定有障碍
        bool left_wall  = (left < LEFT_RIGHT_DIST_THRESH);
        
        int decision = DecideTurn(right_wall, front_wall, left_wall);

		// 修正打印
		const char* dstr[] = {"Left", "Forward", "Right", "U-turn"};
		sprintf(msg, "[DEC] R_wall=%d L_wall=%d -> %s\r\n", right_wall, left_wall, dstr[decision+1]);
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);

		// 计算新目标角度（修正方向）
		float new_target = target_yaw;
		if (decision == 1) new_target -= 90.0f;      // 右转
		else if (decision == -1) new_target += 90.0f; // 左转
		else if (decision == 2) new_target += 180.0f; // 掉头

		// 归一化
		if (new_target >= 360.0f) new_target -= 360.0f;
		if (new_target < 0.0f) new_target += 360.0f;

		if (decision == 0) continue;
		TurnToTarget(new_target);
    }
    
    Set_Motor(0, 0);
    mode = MANUAL;
    sprintf(msg, "[MAZE] Finished\r\n");
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
}

void Avoidance_Stop(void)
{
    maze_running = 0;
}