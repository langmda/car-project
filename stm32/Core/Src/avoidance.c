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
#define SIDE_SCAN_INTERVAL_MS   175     // 左右扫描间隔 (ms)
#define SIDE_CORRECT_THRESH     30.0f   // 触发修正的距离阈值 (cm)
#define YAW_CORRECT_STEP        6.0f    // 每次修正的航向角步长 (度)

#define FORWARD_SPEED           650     // 直行速度 PWM
#define FRONT_DIST_THRESH       20.0f   // 前方障碍阈值 (cm)
#define LEFT_RIGHT_DIST_THRESH  45.0f   // 左右方障碍阈值 (cm)
#define TURN_DELAY_MS           100     // 停车后等待转向的时间
#define YAW_KP                  -10.0f   // 直行时角度补偿系数
#define SCAN_DELAY_MS           120      // 舵机稳定时间

// 舵机角度
#define ANGLE_FRONT             90
#define ANGLE_LEFT              180
#define ANGLE_RIGHT             0

#define YAW_STRAIGHT_KP   10.0f

static volatile uint8_t maze_running = 1;
extern volatile uint8_t mode;

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

    Set_Angle(ANGLE_FRONT);
    HAL_Delay(200);
    for (int i = 0; i < 10; i++) GetCurrentYaw();   // 预热 DMP
    float init_yaw = GetCurrentYaw();
    if (init_yaw < 0) init_yaw += 360.0f;

    float base_target    = init_yaw;
    float current_target = base_target;

    sprintf(msg, "[MAZE] Start, base_target=%.1f\r\n", base_target);
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);

    maze_running = 1;

    while (maze_running) {
        uint32_t last_front_check = HAL_GetTick();
        uint32_t last_side_scan   = HAL_GetTick();

        // ---- 直行循环：改用双环 ----
        while (maze_running) {
            GoStraightWithYaw(FORWARD_SPEED, current_target);      // ← 双环代替原来的 GoStraightWithYaw
            uint32_t now = HAL_GetTick();

            // 前方障碍检测（每 100ms）
            if (now - last_front_check >= 100) {
                last_front_check = now;
                float front = GetDistanceAt(ANGLE_FRONT);
                sprintf(msg, "[FRONT] %.1f cm\r\n", front);
                HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
                if (front < FRONT_DIST_THRESH) break;
            }

            // 左右扫描与临时航向修正（每 200ms）
            if (now - last_side_scan >= 200) {
                last_side_scan = now;
                float left  = GetDistanceAt(ANGLE_LEFT);
                float right = GetDistanceAt(ANGLE_RIGHT);
                Set_Angle(ANGLE_FRONT);

                sprintf(msg, "[SIDE] L=%.1f R=%.1f\r\n", left, right);
                HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);

                uint8_t changed = 0;
                if (right < SIDE_CORRECT_THRESH) {
                    current_target += YAW_CORRECT_STEP;
                    changed = 1;
                } else if (left < SIDE_CORRECT_THRESH) {
                    current_target -= YAW_CORRECT_STEP;
                    changed = 1;
                }

                // 限幅 ±15°
                float diff = current_target - base_target;
                if (diff >  15.0f) current_target = base_target + 15.0f;
                if (diff < -15.0f) current_target = base_target - 15.0f;

                // 归一化
                if (current_target >= 360.0f) current_target -= 360.0f;
                if (current_target < 0.0f)    current_target += 360.0f;

                if (changed) {
                    sprintf(msg, "[CORRECT] cur_target=%.1f\r\n", current_target);
                    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
                }
            }

            HAL_Delay(10);
        }

        if (!maze_running) break;

        // ---- 前方有障碍：停车测左右、决策转向 ----
        Set_Motor(0, 0);
        HAL_Delay(TURN_DELAY_MS);

        float left, right;
        GetLeftRightDistances(&left, &right);
        sprintf(msg, "[LR] Left=%.1f, Right=%.1f\r\n", left, right);
        HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);

        bool right_wall = (right < LEFT_RIGHT_DIST_THRESH);
        bool left_wall  = (left  < LEFT_RIGHT_DIST_THRESH);
        bool front_wall = true;
        int decision = DecideTurn(right_wall, front_wall, left_wall);

        const char* dstr[] = {"Left", "Forward", "Right", "U-turn"};
        sprintf(msg, "[DEC] R_wall=%d L_wall=%d -> %s\r\n",
                right_wall, left_wall, dstr[decision+1]);
        HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);

        float new_base = base_target;
        if      (decision ==  1) new_base -= 90.0f;
        else if (decision == -1) new_base += 90.0f;
        else if (decision ==  2) new_base += 180.0f;

        if (new_base >= 360.0f) new_base -= 360.0f;
        if (new_base <  0.0f)   new_base += 360.0f;

        if (decision != 0) {
            TurnToTarget(new_base);
            base_target    = new_base;
            current_target = new_base;
        }
    }

    Set_Motor(0, 0);
    PID_Reset_Yaw_Reference();   // 退出避障时释放外环目标，便于切回手动
    mode = MANUAL;
    sprintf(msg, "[MAZE] Finished\r\n");
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
}

void Avoidance_Stop(void)
{
    maze_running = 0;
}