#include "infrared.h"
#include "motor.h"
#include "delay.h"
#include "i2c.h"
#include "usart.h"
#include "stdio.h"
#include <math.h>
#include <string.h>

#define SENSOR_ADDR (0x18 << 1)
#define REG_ADDR    0x05

#define BASE_SPEED    1200
#define STEERING_LIMIT_NORMAL  400
#define STEERING_LIMIT_TURN    800
#define LOST_LINE_FWD_RATIO 0.7f

static float Kp_base = 60.0f;
static float Ki = 0.1f;
static float Kd = 12.0f;
static float integral = 0;
static float last_error = 0;
static const float INTEGRAL_LIMIT = 80;

/* ============================================================
 *  last_valid_dir: 带时间戳的“上次明确偏向”
 *  - 超过 LAST_VALID_TIMEOUT_MS 未刷新 → 视为过期，返回 0
 *  - 通过 Update_Last_Valid_Dir() 写入（自动记录时间戳）
 *  - 通过 Get_Last_Valid_Dir()   读取（自动判定过期）
 * ============================================================ */
static int8_t   last_valid_dir = 0;
static uint32_t last_valid_ms  = 0;
#define LAST_VALID_TIMEOUT_MS 2000

static void Update_Last_Valid_Dir(int8_t dir)
{
    last_valid_dir = dir;
    last_valid_ms  = HAL_GetTick();
}

static int8_t Get_Last_Valid_Dir(void)
{
    if (last_valid_dir == 0) return 0;
    if ((HAL_GetTick() - last_valid_ms) > LAST_VALID_TIMEOUT_MS) return 0;
    return last_valid_dir;
}

/* 误差符号历史 (投票后备) */
#define ERR_HIST_LEN 5
static int8_t err_sign_history[ERR_HIST_LEN] = {0};
static uint8_t err_hist_index = 0;

/* ============================================================
 *  单边方向锁定: 时间戳版
 *  - pending_turn_dir 由最外两路触发
 *  - unlock_start_ms   之后 UNLOCK_DELAY_MS 内不允许改动
 *  - straight_start_ms 连续直行满 STRAIGHT_MS 才解锁
 * ============================================================ */
static int8_t   pending_turn_dir = 0;
static uint32_t unlock_start_ms  = 0;
#define UNLOCK_DELAY_MS 150

static uint32_t straight_start_ms = 0;
#define STRAIGHT_MS 200

static void Read_Sensors(uint8_t *sensor)
{
    uint8_t raw_data;
    if (HAL_I2C_Mem_Read(&hi2c2, SENSOR_ADDR, REG_ADDR, I2C_MEMADD_SIZE_8BIT, &raw_data, 1, 100) != HAL_OK) {
        for (int i = 0; i < 5; i++) sensor[i] = 0;
        return;
    }
    uint8_t raw[5];
    raw[0] = (raw_data >> 4) & 0x01;
    raw[1] = (raw_data >> 3) & 0x01;
    raw[2] = (raw_data >> 2) & 0x01;
    raw[3] = (raw_data >> 1) & 0x01;
    raw[4] = (raw_data >> 0) & 0x01;

    sensor[0] = !raw[4];
    sensor[1] = !raw[3];
    sensor[2] = !raw[2];
    sensor[3] = !raw[1];
    sensor[4] = !raw[0];
}

static float Get_Position_Error(uint8_t *sensor)   // 加权计算误差
{
    int16_t numerator = 0, denominator = 0;
    int8_t weights[] = {-2, -1, 0, 1, 2};
    for (int i = 0; i < 5; i++) {
        if (sensor[i]) {
            numerator += weights[i];
            denominator++;
        }
    }
    if (denominator == 0) return 0.0f;
    return (float)numerator / denominator;
}

static uint8_t Handle_Extreme_Turn(uint8_t *sensor)  // 大偏差：只有最左/最右一路黑
{
    if (sensor[0] && !sensor[1] && !sensor[2] && !sensor[3] && !sensor[4]) {
        Set_Motor(-BASE_SPEED, BASE_SPEED);
        uint32_t timeout = HAL_GetTick() + 800;
        while (HAL_GetTick() < timeout) {
            uint8_t tmp[5];
            Read_Sensors(tmp);
            if (tmp[2]) break;
            delay_ms(10);
        }
        Set_Motor(0, 0);
        delay_ms(30);
        Update_Last_Valid_Dir(-1);
        return 1;
    }
    if (!sensor[0] && !sensor[1] && !sensor[2] && !sensor[3] && sensor[4]) {
        Set_Motor(BASE_SPEED, -BASE_SPEED);
        uint32_t timeout = HAL_GetTick() + 800;
        while (HAL_GetTick() < timeout) {
            uint8_t tmp[5];
            Read_Sensors(tmp);
            if (tmp[2]) break;
            delay_ms(10);
        }
        Set_Motor(0, 0);
        delay_ms(30);
        Update_Last_Valid_Dir(1);
        return 1;
    }
    return 0;
}

static uint8_t Handle_Curve_Turn(uint8_t *sensor)   // 小偏差硬编码弯道
{
    if (sensor[0] && sensor[1] && !sensor[2] && !sensor[3] && !sensor[4]) {
        int16_t turn_pwm = BASE_SPEED + 200;
        int16_t left_pwm  = BASE_SPEED - turn_pwm;
        int16_t right_pwm = BASE_SPEED + turn_pwm;
        left_pwm  = (left_pwm  > 4320) ? 4320 : (left_pwm  < -4320) ? -4320 : left_pwm;
        right_pwm = (right_pwm > 4320) ? 4320 : (right_pwm < -4320) ? -4320 : right_pwm;
        Set_Motor(left_pwm, right_pwm);
        delay_ms(60);
        Update_Last_Valid_Dir(-1);
        return 1;
    }
    if (!sensor[0] && !sensor[1] && !sensor[2] && sensor[3] && sensor[4]) {
        int16_t turn_pwm = BASE_SPEED + 200;
        int16_t left_pwm  = BASE_SPEED + turn_pwm;
        int16_t right_pwm = BASE_SPEED - turn_pwm;
        left_pwm  = (left_pwm  > 4320) ? 4320 : (left_pwm  < -4320) ? -4320 : left_pwm;
        right_pwm = (right_pwm > 4320) ? 4320 : (right_pwm < -4320) ? -4320 : right_pwm;
        Set_Motor(left_pwm, right_pwm);
        delay_ms(60);
        Update_Last_Valid_Dir(1);
        return 1;
    }
    if (sensor[1] && !sensor[0] && !sensor[2] && !sensor[3] && !sensor[4]) {
        int16_t turn_pwm = BASE_SPEED;
        int16_t left_pwm  = BASE_SPEED - turn_pwm;
        int16_t right_pwm = BASE_SPEED + turn_pwm;
        left_pwm  = (left_pwm  > 4320) ? 4320 : (left_pwm  < -4320) ? -4320 : left_pwm;
        right_pwm = (right_pwm > 4320) ? 4320 : (right_pwm < -4320) ? -4320 : right_pwm;
        Set_Motor(left_pwm, right_pwm);
        delay_ms(40);
        Update_Last_Valid_Dir(-1);
        return 1;
    }
    if (sensor[3] && !sensor[0] && !sensor[1] && !sensor[2] && !sensor[4]) {
        int16_t turn_pwm = BASE_SPEED;
        int16_t left_pwm  = BASE_SPEED + turn_pwm;
        int16_t right_pwm = BASE_SPEED - turn_pwm;
        left_pwm  = (left_pwm  > 4320) ? 4320 : (left_pwm  < -4320) ? -4320 : left_pwm;
        right_pwm = (right_pwm > 4320) ? 4320 : (right_pwm < -4320) ? -4320 : right_pwm;
        Set_Motor(left_pwm, right_pwm);
        delay_ms(40);
        Update_Last_Valid_Dir(1);
        return 1;
    }
    return 0;
}

/*
 * 丢线恢复：慢速后退 + 原地旋转，直到中间红外重新捕获黑线。
 * 旋转方向由 pending_turn_dir 或投票决定。
 */
static uint8_t Handle_Lost_Line(uint8_t *sensor)
{
    if (!sensor[0] && !sensor[1] && !sensor[2] && !sensor[3] && !sensor[4]) {
        int8_t lost_dir = 0;

        /* 1. 决定旋转方向 */
        if (pending_turn_dir != 0) {
            lost_dir = pending_turn_dir;
        } else {
            int8_t vote_left = 0, vote_right = 0;
            for (int i = 0; i < ERR_HIST_LEN; i++) {
                if (err_sign_history[i] == -1) vote_left++;
                else if (err_sign_history[i] == 1) vote_right++;
            }
            if (vote_right > vote_left + 1)      lost_dir = 1;
            else if (vote_left > vote_right + 1) lost_dir = -1;
            else                                 lost_dir = Get_Last_Valid_Dir();
            if (lost_dir == 0) lost_dir = -1;
        }

        char buf[64];
        sprintf(buf, "[LOST] %s dir: %d\r\n",
                pending_turn_dir ? "PendingTurn" : "Vote", lost_dir);
        HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 100);

        /* 2. 立即停车，消除惯性 */
        Set_Motor(0, 0);
        delay_ms(100);

        /* 3. 低速旋转参数 */
        int16_t backward_base = 350;
        int16_t rotate_min    = 300;
        int16_t rotate_max    = 800;
        int16_t rotate_step   = 50;
        int16_t rotate_pwm    = rotate_min;

        uint32_t start = HAL_GetTick();
        uint8_t found = 0;

        /* 4. 慢速旋转搜索，超时 4000ms */
        while ((HAL_GetTick() - start) < 4000) {
            int16_t left_pwm, right_pwm;
            if (lost_dir < 0) {          // 左转
                left_pwm  = -rotate_pwm - backward_base;
                right_pwm =  rotate_pwm - backward_base;
            } else {                     // 右转
                left_pwm  =  rotate_pwm - backward_base;
                right_pwm = -rotate_pwm - backward_base;
            }

            left_pwm  = (left_pwm  > 7200) ?  7200 : (left_pwm  < -7200) ? -7200 : left_pwm;
            right_pwm = (right_pwm > 7200) ?  7200 : (right_pwm < -7200) ? -7200 : right_pwm;

            Set_Motor(left_pwm, right_pwm);
            delay_ms(25);

            uint8_t tmp[5];
            Read_Sensors(tmp);
            if (tmp[2]) {
                Set_Motor(0, 0);
                delay_ms(30);
                found = 1;
                break;
            }

            if (rotate_pwm < rotate_max) {
                rotate_pwm += rotate_step;
            }
        }

        /* 5. 最终停止 */
        Set_Motor(0, 0);
        delay_ms(50);

        if (found) {
            straight_start_ms = 0;
            memset(err_sign_history, 0, sizeof(err_sign_history));
            err_hist_index = 0;
            delay_ms(30);
            return 1;
        } else {
            char buf2[64];
            sprintf(buf2, "[ERROR] Lost line, stop\r\n");
            HAL_UART_Transmit(&huart2, (uint8_t*)buf2, strlen(buf2), 100);
            return 1;
        }
    }
    return 0;
}

static uint8_t Count_Left_Continuous(uint8_t *sensor)
{
    uint8_t cnt = 0;
    for (int i = 0; i < 5; i++) {
        if (sensor[i]) cnt++;
        else break;
    }
    return cnt;
}

static uint8_t Count_Right_Continuous(uint8_t *sensor)
{
    uint8_t cnt = 0;
    for (int i = 4; i >= 0; i--) {
        if (sensor[i]) cnt++;
        else break;
    }
    return cnt;
}

static void Resolve_Bilateral_Conflict(uint8_t *sensors)
{
    uint8_t left_cnt  = Count_Left_Continuous(sensors);
    uint8_t right_cnt = Count_Right_Continuous(sensors);

    if (left_cnt >= 2 && right_cnt >= 2) {
        int8_t lv = Get_Last_Valid_Dir();       // 带过期判定
        if (lv == 1) {
            for (int i = 0; i < left_cnt; i++) sensors[i] = 0;
        } else if (lv == -1) {
            for (int i = 4; i >= (5 - right_cnt); i--) sensors[i] = 0;
        } else {
            if (left_cnt > right_cnt) {
                for (int i = 4; i >= (5 - right_cnt); i--) sensors[i] = 0;
            } else {
                for (int i = 0; i < left_cnt; i++) sensors[i] = 0;
            }
        }
    }
}

void Line_Follow_Task(void)
{
    static uint32_t last_time = 0;
    uint32_t now = HAL_GetTick();
    if (now - last_time < 10) return;
    last_time = now;

    uint8_t sensors[5];
    Read_Sensors(sensors);

    uint8_t all_lost = !sensors[0] && !sensors[1] && !sensors[2] && !sensors[3] && !sensors[4];

    /* ---- 锁定逻辑：最外两路触发时设方向，并刷新冷静期时间戳 ---- */
    if (!all_lost) {
        if (sensors[0] == 1 && sensors[4] == 0) {
            if (pending_turn_dir != -1) {
                pending_turn_dir = -1;
                unlock_start_ms  = now;
                straight_start_ms = 0;
            }
        } else if (sensors[0] == 0 && sensors[4] == 1) {
            if (pending_turn_dir != 1) {
                pending_turn_dir = 1;
                unlock_start_ms  = now;
                straight_start_ms = 0;
            }
        }
    }

    /* ---- 丢线处理 ---- */
    if (Handle_Lost_Line(sensors)) return;

    /* ---- 极端转弯 ---- */
    if (Handle_Extreme_Turn(sensors)) return;

    /* ---- 双侧冲突仲裁 ---- */
    Resolve_Bilateral_Conflict(sensors);

    /* ---- 硬编码弯道 ---- */
    if (Handle_Curve_Turn(sensors)) return;

    /* ---- 正常 PID ---- */
    float error = Get_Position_Error(sensors);

    if (fabs(error) >= 0.5f) {
        Update_Last_Valid_Dir((error < 0) ? -1 : 1);
    }

    int8_t sign = 0;
    if      (error >  0.2f) sign = 1;
    else if (error < -0.2f) sign = -1;
    err_sign_history[err_hist_index] = sign;
    err_hist_index = (err_hist_index + 1) % ERR_HIST_LEN;

    /* ---- 解锁管理：冷静期 + 连续直行计时 ---- */
    if ((now - unlock_start_ms) < UNLOCK_DELAY_MS) 
	{
        straight_start_ms = 0;			//冷静期内不解锁
    } 
	else if (!all_lost && pending_turn_dir != 0) 		//未丢线且方向变量不为空，如果能将黑线稳定在中间，持续200ms，将清空方向变量
	{
        if (fabs(error) < 0.2f && sensors[2] == 1 && sensors[0] == 0 && sensors[4] == 0) 		//这次检测中黑线稳定在中间
		{
            if (straight_start_ms == 0) 							//还未开始检测黑线是否能稳定在中间，开始检测
				straight_start_ms = now;
            if ((now - straight_start_ms) >= STRAIGHT_MS) 			//黑线持续稳定超过200ms，清空方向变量
			{
                pending_turn_dir  = 0;
                straight_start_ms = 0;
            }
        }
		else										//这次检测中黑线不在中间，清空计时，下次重新开始检测
		{	
            straight_start_ms = 0;
        }
    }
	else 											//丢线或方向变量为空，清空计时，等待下次设置方向变量
	{
        straight_start_ms = 0;
    }

    /* ---- 直行死区 ---- */
    if (fabs(error) < 0.4f) {
        Set_Motor(BASE_SPEED, BASE_SPEED);
        integral = 0;
        static uint32_t last_print = 0;
        if (now - last_print > 200) {
            char buf[80];
            sprintf(buf, "S:%d%d%d%d%d Err:%.2f Straight Pending:%d\r\n",
                    sensors[0], sensors[1], sensors[2], sensors[3], sensors[4],
                    error, pending_turn_dir);
            HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 100);
            last_print = now;
        }
        return;
    }

    /* ---- PID 控制 ---- */
    float Kp = Kp_base;
    if (fabs(error) > 1.0f) Kp = Kp_base * 1.5f;

    float dt = 0.01f;
    integral += error * Ki * dt;
    if (integral >  INTEGRAL_LIMIT) integral =  INTEGRAL_LIMIT;
    if (integral < -INTEGRAL_LIMIT) integral = -INTEGRAL_LIMIT;
    float p = Kp * error;
    float i = integral;
    float d = Kd * (error - last_error) / dt;
    last_error = error;
    float steering = p + i + d;

    int16_t max_steer = STEERING_LIMIT_NORMAL;
    if (fabs(error) > 1.2f) max_steer = STEERING_LIMIT_TURN;
    if (steering >  max_steer) steering =  max_steer;
    if (steering < -max_steer) steering = -max_steer;

    int16_t left_pwm  = BASE_SPEED + (int16_t)steering;
    int16_t right_pwm = BASE_SPEED - (int16_t)steering;
    left_pwm  = (left_pwm  > 4320) ? 4320 : (left_pwm  < -4320) ? -4320 : left_pwm;
    right_pwm = (right_pwm > 4320) ? 4320 : (right_pwm < -4320) ? -4320 : right_pwm;
    Set_Motor(left_pwm, right_pwm);

    static uint32_t last_print = 0;
    if (now - last_print > 200) {
        char buf[80];
        sprintf(buf, "S:%d%d%d%d%d Err:%.2f Steer:%.1f LastDir:%d Pending:%d\r\n",
                sensors[0], sensors[1], sensors[2], sensors[3], sensors[4],
                error, steering, Get_Last_Valid_Dir(), pending_turn_dir);
        HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 100);
        last_print = now;
    }
}

void Line_Follow_Init(void)
{
    integral = 0;
    last_error = 0;

    last_valid_dir = 0;
    last_valid_ms  = 0;

    pending_turn_dir  = 0;
    unlock_start_ms   = 0;
    straight_start_ms = 0;

    memset(err_sign_history, 0, sizeof(err_sign_history));
    err_hist_index = 0;
}