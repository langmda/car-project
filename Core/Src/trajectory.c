#include "trajectory.h"
#include "motor.h"
#include "inv_mpu.h"
#include "delay.h"
#include "usart.h"
#include "stdio.h"
#include <math.h>
#include "pid.h"
#include "tim.h"

#define PULSE_PER_ROUND 1560
#define WHEEL_DIAMETER 85

static char debug_buf[128];

// 原地转向（正角度为左转，负角度为右转）
void turn_angle_blocking(int16_t target_angle_deg) 
{
	__HAL_TIM_SET_COUNTER(&htim2, 0);
    __HAL_TIM_SET_COUNTER(&htim4, 0);
    encoder_left = 0;
    encoder_right = 0;
    // ========== 校准系数 ==========
    // 如果实际转100° 测量只有85°，则系数 = 85/100 = 0.85
    // 通过实验确定：先设为1.0，测试90°命令，测量实际转角，然后调整
    const float ANGLE_SCALE = 1.0f;   // 根据你的实测结果调整（0.7 ~ 1.3）
    // ===============================
    
    float current_yaw, pitch, roll;
    float start_yaw, target_yaw;
    float error;
    int16_t turn_pwm;
    uint32_t timeout_ms = 8000;
    uint32_t start_time = HAL_GetTick();
    const float angle_tolerance = 1.0f;
    const int16_t max_turn_pwm = 2500;
    const int16_t min_turn_pwm = 120;
    uint16_t iter = 0;
    float abs_err;
    float last_valid_yaw = 0;
    uint8_t dmp_fail_count = 0;
    const uint8_t MAX_DMP_FAIL = 5;
    float last_yaw = 0;
    uint8_t stuck_count = 0;
    uint8_t dmp_ret;
    uint8_t retry;
    float new_yaw;

    // 预热DMP，丢弃前几次不稳定数据
    for (int i = 0; i < 5; i++) {
        retry = 0;
        dmp_ret = 1;
        while (retry < 3 && dmp_ret != 0) {
            dmp_ret = mpu_dmp_get_data(&pitch, &roll, &new_yaw);
            if (dmp_ret != 0) {
                retry++;
                delay_ms(2);
            }
        }
        delay_ms(5);
    }

    // 1. 读取当前角度作为起点（带重试）
    retry = 0;
    dmp_ret = 1;
    while (retry < 3 && dmp_ret != 0) {
        dmp_ret = mpu_dmp_get_data(&pitch, &roll, &new_yaw);
        if (dmp_ret != 0) {
            retry++;
            delay_ms(2);
        }
    }
    if (dmp_ret != 0) {
        sprintf(debug_buf, "[ERR] DMP read failed at start\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
        return;
    }
    
    // 转换到 0-360 范围
    current_yaw = new_yaw;
    if (current_yaw < 0) current_yaw += 360.0f;
    start_yaw = current_yaw;
    
    // 应用校准系数
    float calibrated_angle = (float)target_angle_deg * ANGLE_SCALE;
    target_yaw = start_yaw + calibrated_angle;
    
    // 归一化目标角度到 0~360 范围
    while (target_yaw >= 360.0f) target_yaw -= 360.0f;
    while (target_yaw < 0.0f) target_yaw += 360.0f;

    sprintf(debug_buf, "[TURN] start=%.1f target=%.1f (req=%d°, scale=%.2f)\r\n", 
            start_yaw, target_yaw, target_angle_deg, ANGLE_SCALE);
    HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
    
    last_valid_yaw = current_yaw;
    last_yaw = current_yaw;

    // 3. 转向控制循环
    while (1) {
        iter++;
        
        // 读取最新角度（带重试）
        retry = 0;
        dmp_ret = 1;
        new_yaw = 0;
        while (retry < 3 && dmp_ret != 0) {
            dmp_ret = mpu_dmp_get_data(&pitch, &roll, &new_yaw);
            if (dmp_ret != 0) {
                retry++;
                delay_ms(2);
            }
        }
        if (dmp_ret == 0) {
            current_yaw = new_yaw;
            if (current_yaw < 0) current_yaw += 360.0f;
            last_valid_yaw = current_yaw;
            dmp_fail_count = 0;
        } else {
            dmp_fail_count++;
            if (dmp_fail_count <= 3) {
                sprintf(debug_buf, "[WARN] DMP fail %d/%d, use last yaw=%.1f\r\n", 
                        dmp_fail_count, MAX_DMP_FAIL, last_valid_yaw);
                HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
            }
            current_yaw = last_valid_yaw;
            if (dmp_fail_count >= MAX_DMP_FAIL) {
                sprintf(debug_buf, "[ERR] DMP persistent failure, abort turn\r\n");
                HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
                break;
            }
        }
        
        // 检测角度是否卡死
        if (iter > 10 && fabs(current_yaw - last_yaw) < 0.2f) {
            stuck_count++;
            if (stuck_count > 30) {
                sprintf(debug_buf, "[ERR] yaw stuck at %.1f, force exit\r\n", current_yaw);
                HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
                break;
            }
        } else {
            stuck_count = 0;
        }
        last_yaw = current_yaw;
        
        // 计算最短角度差
        error = target_yaw - current_yaw;
        if (error > 180.0f) error -= 360.0f;
        if (error < -180.0f) error += 360.0f;
        abs_err = (error < 0) ? -error : error;
        
        // 打印调试信息（每5次迭代打印一次）
        if (iter % 5 == 0 || iter <= 5) {
            sprintf(debug_buf, "iter=%d, yaw=%.1f, err=%.1f, abs=%.1f\r\n", 
                    iter, current_yaw, error, abs_err);
            HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
        }
        
        // 退出条件：误差小于允许值
        if (abs_err <= angle_tolerance) {
            // 增强制动，消除惯性过冲
            Set_Motor(0, 0);
            delay_ms(10);
            // 根据最后转向方向施加反向制动
            int16_t brake_pwm = (turn_pwm > 0) ? -200 : 200;
            Set_Motor(-brake_pwm, brake_pwm);
            delay_ms(150);
            Set_Motor(0, 0);
            sprintf(debug_buf, "[TURN] reached tolerance at iter=%d, yaw=%.1f\r\n", iter, current_yaw);
            HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
            break;
        }
        
        // 超时保护
        if ((HAL_GetTick() - start_time) > timeout_ms) {
            sprintf(debug_buf, "[TURN] timeout after %d ms, yaw=%.1f\r\n", timeout_ms, current_yaw);
            HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
            break;
        }
        
        // 安全迭代次数限制
        if (iter > 1000) {
            sprintf(debug_buf, "[TURN] max iter reached (%d)\r\n", iter);
            HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
            break;
        }
        
        // 比例控制 + 多级减速
        turn_pwm = (int16_t)(error * 20.0f);
        if (abs_err < 10.0f) {
            turn_pwm = (int16_t)(error * 12.0f);
        }
        if (abs_err < 5.0f) {
            turn_pwm = (int16_t)(error * 6.0f);
        }
        if (abs_err < 2.0f) {
            turn_pwm = (int16_t)(error * 3.0f);
        }
        
        // 限制最大最小 PWM
        if (turn_pwm > max_turn_pwm) turn_pwm = max_turn_pwm;
        if (turn_pwm < -max_turn_pwm) turn_pwm = -max_turn_pwm;
        // 克服电机死区
        if (turn_pwm > 0 && turn_pwm < min_turn_pwm) turn_pwm = min_turn_pwm;
        if (turn_pwm < 0 && turn_pwm > -min_turn_pwm) turn_pwm = -min_turn_pwm;
        
        // 打印 PWM（前几次）
        if (iter <= 5) {
            sprintf(debug_buf, "pwm=%d\r\n", turn_pwm);
            HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
        }
        
        // 设置电机：左转时左轮负、右轮正；右转相反
        Set_Motor(-turn_pwm, turn_pwm);
        
        delay_ms(10);
    }
    
    // 停止电机
    Set_Motor(0, 0);
    sprintf(debug_buf, "[TURN] finished after %d iterations\r\n", iter);
    HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
    delay_ms(50);
}

// ==================== 前进指定距离（阻塞式） ====================
// 参数：distance_mm 目标距离（毫米），正数为前进
void go_straight_blocking(int32_t distance_mm)
{
    if (distance_mm == 0) return;
    
    const float wheel_circumference = 3.1415926f * WHEEL_DIAMETER;
    const float pulse_per_mm = (float)PULSE_PER_ROUND / wheel_circumference;
    int32_t target_pulse = (int32_t)((float)distance_mm * pulse_per_mm);
    
    int32_t start_total_left = total_encoder_left;
    int32_t start_total_right = total_encoder_right;
    
    uint32_t timeout_ms = 15000;  // 15秒超时
    uint32_t start_time = HAL_GetTick();
    int32_t traveled_pulse = 0;
    
    sprintf(debug_buf, "[GO] distance=%ld mm, target_pulse=%ld, pulse/mm=%.2f\r\n", 
            distance_mm, target_pulse, pulse_per_mm);
    HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
    
    while (1) {
        PID_Go_Straight(2000);
        
        int32_t delta_left = total_encoder_left - start_total_left;
        int32_t delta_right = total_encoder_right - start_total_right;
        traveled_pulse = (delta_left + delta_right) / 2;
        
        static uint32_t last_print = 0;
        uint32_t now = HAL_GetTick();
        if (now - last_print > 100) {
            sprintf(debug_buf, "[GO] traveled=%ld / %ld, left_delta=%ld, right_delta=%ld\r\n", 
                    traveled_pulse, target_pulse, delta_left, delta_right);
            HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
            last_print = now;
        }
        
        if (traveled_pulse >= target_pulse) break;
        if ((HAL_GetTick() - start_time) > timeout_ms) {
            sprintf(debug_buf, "[GO] timeout\r\n");
            HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
            break;
        }
        
        // 不加任何 delay，让循环全速运行
        // PID_Go_Straight 内部已经以 20ms 为周期更新编码器
    }
    
    Set_Motor(0, 0);
    delay_ms(20);
    Set_Motor(-100, -100);
    delay_ms(30);
    Set_Motor(0, 0);
    
    sprintf(debug_buf, "[GO] finished, traveled=%ld pulses\r\n", traveled_pulse);
    HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
}


void arc_blocking(int16_t delta_angle_deg, int16_t forward_speed)
{
    float current_yaw, pitch, roll;
    float start_yaw;
    float rotated_angle = 0.0f;
    int16_t turn_pwm = 400;          // 固定转向差速（可调，值越大转弯半径越小）
    uint32_t timeout_ms = 10000;
    uint32_t start_time = HAL_GetTick();
    const float angle_tolerance = 5.0f;

    // 重置 PID 和编码器滤波状态（避免后续直行干扰）
    PI_Param_Clear();
    extern int16_t last_left, last_right;
    last_left = 0;
    last_right = 0;

    // 预热 DMP
    for (int i = 0; i < 5; i++) {
        mpu_dmp_get_data(&pitch, &roll, &current_yaw);
        delay_ms(5);
    }

    // 读取起始角度
    mpu_dmp_get_data(&pitch, &roll, &current_yaw);
    if (current_yaw < 0) current_yaw += 360.0f;
    start_yaw = current_yaw;

    sprintf(debug_buf, "[ARC] start=%.1f delta=%d speed=%d turn=%d\r\n", 
            start_yaw, delta_angle_deg, forward_speed, turn_pwm);
    HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);

    // 根据目标方向设置电机差速（绝对稳定）
    int16_t left_pwm, right_pwm;
    if (delta_angle_deg > 0) {          // 右转：左轮慢、右轮快
        left_pwm = forward_speed - turn_pwm;
        right_pwm = forward_speed + turn_pwm;
    } else {                            // 左转：左轮快、右轮慢
        left_pwm = forward_speed + turn_pwm;
        right_pwm = forward_speed - turn_pwm;
    }
    // 限幅
    if (left_pwm > 4320) left_pwm = 4320;
    if (left_pwm < -4320) left_pwm = -4320;
    if (right_pwm > 4320) right_pwm = 4320;
    if (right_pwm < -4320) right_pwm = -4320;
    Set_Motor(left_pwm, right_pwm);

    // 角度积分循环
    while (1) {
        mpu_dmp_get_data(&pitch, &roll, &current_yaw);
        if (current_yaw < 0) current_yaw += 360.0f;

        // 计算已转过的角度（处理跨零点）
        float diff = current_yaw - start_yaw;
        if (diff > 180.0f) diff -= 360.0f;
        if (diff < -180.0f) diff += 360.0f;
        rotated_angle = diff;

        // 打印调试信息（每 200ms）
        static uint32_t last_print = 0;
        uint32_t now = HAL_GetTick();
        if (now - last_print > 200) {
            sprintf(debug_buf, "[ARC] yaw=%.1f rotated=%.1f / %d\r\n", 
                    current_yaw, rotated_angle, delta_angle_deg);
            HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
            last_print = now;
        }

        // 退出条件：达到目标角度（考虑方向）
        if (delta_angle_deg > 0) {
            if (rotated_angle >= delta_angle_deg - angle_tolerance) break;
        } else {
            if (rotated_angle <= delta_angle_deg + angle_tolerance) break;
        }

        if ((HAL_GetTick() - start_time) > timeout_ms) {
            sprintf(debug_buf, "[ARC] timeout\r\n");
            HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
            break;
        }
        delay_ms(10);
    }

    // 停止并清理状态
    Set_Motor(0, 0);
    PI_Param_Clear();
    last_left = 0;
    last_right = 0;
    sprintf(debug_buf, "[ARC] finished, rotated=%.1f\r\n", rotated_angle);
    HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 100);
}

void trajectory_Z()
{
	go_straight_blocking(600);
	turn_angle_blocking(-120);
	go_straight_blocking(1000);
	turn_angle_blocking(120);
	go_straight_blocking(600);
}
void trajectory_J()
{
	go_straight_blocking(1000);
	arc_blocking(-160, 1500);
}
void trajectory_U()
{
	go_straight_blocking(1000);
	arc_blocking(-160, 1000);
	go_straight_blocking(1000);
}