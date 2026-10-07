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
#include "tim.h"
#include "sensor.h"

// ---------- 可调参数 ----------
#define SIDE_SCAN_INTERVAL_MS   175     // 左右扫描间隔 (ms)
#define SIDE_CORRECT_THRESH     30.0f   // 触发修正的距离阈值 (cm)
#define YAW_CORRECT_STEP        6.0f    // 每次修正的航向角步长 (度)

#define FORWARD_SPEED           650     // 直行速度 PWM
#define FRONT_DIST_THRESH       20.0f   // 前方障碍阈值 (cm)
#define LEFT_RIGHT_DIST_THRESH  45.0f   // 左右方障碍阈值 (cm)
#define TURN_DELAY_MS           100     // 停车后等待转向的时间
#define SCAN_DELAY_MS           120      // 舵机稳定时间

// 舵机角度
#define ANGLE_FRONT             90
#define ANGLE_LEFT              180
#define ANGLE_RIGHT             0

static volatile uint8_t maze_running = 1;  //标志位，表示当前是否正处于避障状态。再进入避障主函数后会变成1，切换模式后变成0，避障主函数随后将退出
extern uint8_t mode;

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

// 获取指定方向的距离（单次）
static float GetDistanceAt(int angle)
{
    Set_Angle(angle);
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < SCAN_DELAY_MS) {
        Read_Encoder();
        delay_ms(5);
    }

    float d1 = Get_Median_Distance(1);   // 你的超声波单次接口（假设）
    float d2 = Get_Median_Distance(1);
    float d3 = Get_Median_Distance(1);
    float dist = (d1 + d2 + d3) / 3.0f;   // 简单均值

    char dbg[96];
    sprintf(dbg, DBG_HEAD "[DIST] angle=%d d1=%.1f d2=%.1f d3=%.1f avg=%.1f" DBG_TAIL,
            angle, d1, d2, d3, dist);
    HAL_UART_Transmit(&huart2, (uint8_t*)dbg, strlen(dbg), 100);

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

    char dbg[128];
    sprintf(dbg, DBG_HEAD "[TURN] cur=%.1f tgt=%.1f delta=%.1f" DBG_TAIL,
            current, new_target, delta);
    HAL_UART_Transmit(&huart2, (uint8_t*)dbg, strlen(dbg), 100);

    turn_angle_blocking((int16_t)delta);

    float after = GetCurrentYaw();
    sprintf(dbg, DBG_HEAD "[TURN-DONE] cur=%.1f tgt=%.1f err=%.1f" DBG_TAIL,
            after, new_target, fabs(after - new_target));
    HAL_UART_Transmit(&huart2, (uint8_t*)dbg, strlen(dbg), 100);

    // ★ 转向后清编码器累积，避免第一次读被软滤波丢弃
    __HAL_TIM_SET_COUNTER(&htim2, 0);
	__HAL_TIM_SET_COUNTER(&htim4, 0);
	encoder_left = 0;
	encoder_right = 0;
	last_left = 0;
	last_right = 0;
	last_pid_time = HAL_GetTick();
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
    for (int i = 0; i < 10; i++) GetCurrentYaw();		//预热mpu6050

    float base_target = GetCurrentYaw();			//获取当前偏航角
    if (base_target < 0) base_target += 360.0f;

    float current_target = base_target;

    // ★ 设置外环初始目标
    PID_Set_Yaw_Target(current_target);				//设置当前航向角

    maze_running = 1;

    while (maze_running) 
	{
        uint32_t last_front_check = HAL_GetTick();
        uint32_t last_side_scan = HAL_GetTick();

        // ---- 直行循环 ----		有两种退出方法：1.用户切换了模式，maze_running变为0，自行结束循环 2. 前方检测到障碍物，break退出循环
        while (maze_running) 
		{
            // ★ 换成双环
            PID_Go_Straight(FORWARD_SPEED);
            uint32_t now = HAL_GetTick();

            // 前方障碍检测，每100ms执行一次，障碍物小于阈值将退出直行循环
			if (now - last_front_check >= 100) {
				last_front_check = now;
				float front = GetDistanceAt(ANGLE_FRONT);

				char dbg[96];
				sprintf(dbg, DBG_HEAD "[FRONT] %.1f cm (thr=%.1f) %s" DBG_TAIL,
						front, FRONT_DIST_THRESH,
						(front < FRONT_DIST_THRESH) ? "BREAK!" : "OK");
				HAL_UART_Transmit(&huart2, (uint8_t*)dbg, strlen(dbg), 100);

				if (front < FRONT_DIST_THRESH) break;
			}

            // 左右扫描与临时修正，每200ms执行一次
            if (now - last_side_scan >= 200) 
			{
                last_side_scan = now;
                float left = GetDistanceAt(ANGLE_LEFT);
                float right = GetDistanceAt(ANGLE_RIGHT);
                Set_Angle(ANGLE_FRONT);

                sprintf(msg, "[SIDE] L=%.1f R=%.1f\r\n", left, right);
                HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);

                bool changed = false;			//标志位，标记这次循环有没有进行角度临时修正
                if (right < SIDE_CORRECT_THRESH) 
				{
                    current_target += YAW_CORRECT_STEP;
                    changed = true;
                } else if (left < SIDE_CORRECT_THRESH) 
				{
                    current_target -= YAW_CORRECT_STEP;
                    changed = true;
                }

                // 限幅
                float diff = current_target - base_target;
                if (diff > 15.0f) current_target = base_target + 15.0f;
                if (diff < -15.0f) current_target = base_target - 15.0f;

                // 归一化
                if (current_target >= 360.0f) current_target -= 360.0f;
                if (current_target < 0.0f) current_target += 360.0f;

                if (changed) 				//如果进行了角度临时修正，将角度应用到航向角度
				{
                    PID_Update_Yaw_Target(current_target);
                }
            }
			if (read_and_transmit == 1) //发送传感器数据，由于避障函数是while阻塞循环的，主函数里就执行不到发送传感器数据了，要放到避障函数里执行
			{
				Read_And_Transmit();
				read_and_transmit = 0;
			}

            HAL_Delay(10);
        }

        if (!maze_running) break;			//如果是用户切换模式造成退出直线循环，那就直接结束避障函数	

        // ---- 转向决策 ----
        Set_Motor(0, 0);
        HAL_Delay(TURN_DELAY_MS);
        float left, right;
        GetLeftRightDistances(&left, &right);

		bool right_wall = (right < LEFT_RIGHT_DIST_THRESH);
		bool left_wall  = (left < LEFT_RIGHT_DIST_THRESH);
		int decision = DecideTurn(right_wall, true, left_wall);

		const char* dstr[] = {"Left", "Forward", "Right", "U-turn"};
		char dbg[128];
		sprintf(dbg, DBG_HEAD "[DEC] L=%.1f R=%.1f Rwall=%d Lwall=%d -> %s" DBG_TAIL,
				left, right, right_wall, left_wall, dstr[decision+1]);
		HAL_UART_Transmit(&huart2, (uint8_t*)dbg, strlen(dbg), 100);

        float new_base = base_target;
        if (decision == 1) new_base -= 90.0f;
        else if (decision == -1) new_base += 90.0f;
        else if (decision == 2) new_base += 180.0f;

        if (new_base >= 360.0f) new_base -= 360.0f;
        if (new_base < 0.0f) new_base += 360.0f;

        if (decision != 0) 
		{
            TurnToTarget(new_base);
            base_target = new_base;
            current_target = base_target;
            // ★ 转向后用清积分的版本
            PID_Set_Yaw_Target(current_target);
        }
    }

    Set_Motor(0, 0);
}

void Avoidance_Stop(void)
{
    maze_running = 0;
}