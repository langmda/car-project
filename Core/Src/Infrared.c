#include "infrared.h"
#include "motor.h"
#include "delay.h"
#include "i2c.h"
#include "usart.h"
#include "stdio.h"
#include <math.h>

// 传感器 I2C 地址和寄存器
#define SENSOR_ADDR (0x18 << 1)
#define REG_ADDR    0x05

// 电机基础速度（前进速度）
#define BASE_SPEED    650    // 降低速度，提高弯道响应

// 转向量最大限幅
#define MAX_STEERING  300

// 丢线搜索时的前进速度（占基础速度的比例）
#define LOST_LINE_FWD_RATIO 0.6f

static int8_t last_turn_direction = 0;

// PID 参数
typedef struct {
    float Kp;
    float Ki;
    float Kd;
    float integral;
    float last_error;
    uint16_t integral_limit;
    uint8_t  integral_enable;
} PID_TypeDef;

static PID_TypeDef pid = {
    .Kp = 55.0f,        // 提高比例系数，增强响应
    .Ki = 0.25f,
    .Kd = 10.0f,
    .integral = 0,
    .last_error = 0,
    .integral_limit = 80,
    .integral_enable = 1
};

// 方向保持相关变量
static int8_t persistent_dir = 0;   // 强制保持的方向（-1左转,1右转,0无）
static uint8_t persistent_cnt = 0;
static const uint8_t HOLD_THRESHOLD = 3;   // 连续3次同一方向即锁定

// 读取五路传感器，返回数组（0-白，1-黑）
static void Read_Sensors(uint8_t *sensor)
{
    uint8_t raw_data;
    if (HAL_I2C_Mem_Read(&hi2c2, SENSOR_ADDR, REG_ADDR, I2C_MEMADD_SIZE_8BIT, &raw_data, 1, 100) != HAL_OK)
    {
        for (int i=0; i<5; i++) sensor[i] = 0;
        return;
    }
    uint8_t raw[5];
    raw[0] = (raw_data >> 4) & 0x01;   // 物理最左
    raw[1] = (raw_data >> 3) & 0x01;
    raw[2] = (raw_data >> 2) & 0x01;
    raw[3] = (raw_data >> 1) & 0x01;
    raw[4] = (raw_data >> 0) & 0x01;   // 物理最右

    // 交换左右，使 sensor[0] 对应逻辑左侧（物理最右）
    sensor[0] = raw[4];
    sensor[1] = raw[3];
    sensor[2] = raw[2];
    sensor[3] = raw[1];
    sensor[4] = raw[0];
}

// 计算位置偏差（范围 -2.0 到 +2.0，0表示线在正中）
static float Get_Position_Error(uint8_t *sensor)
{
    int16_t numerator = 0;
    int16_t denominator = 0;
    int8_t weights[] = {-2, -1, 0, 1, 2};
    for (int i=0; i<5; i++) {
        if (sensor[i]) {
            numerator += weights[i];
            denominator++;
        }
    }
    if (denominator == 0) return 0.0f;
    return (float)numerator / denominator;
}

// 边缘转弯（边前进边转弯），用于处理弯角过急
static uint8_t Handle_Edge_Turn(uint8_t *sensor)
{
    // 情况1：左侧两个传感器检测到黑线（S1+S2） -> 边左转边前进
    if (sensor[0] && sensor[1] && !sensor[2] && !sensor[3] && !sensor[4]) {
        int16_t left_pwm = BASE_SPEED - 450;
        int16_t right_pwm = BASE_SPEED + 450;
        if (left_pwm < 0) left_pwm = 0;
        if (right_pwm > 4320) right_pwm = 4320;
        Set_Motor(left_pwm, right_pwm);
        delay_ms(60);
        return 1;
    }
    // 情况2：右侧两个传感器检测到黑线（S4+S5） -> 边右转边前进
    if (!sensor[0] && !sensor[1] && !sensor[2] && sensor[3] && sensor[4]) {
        int16_t left_pwm = BASE_SPEED + 450;
        int16_t right_pwm = BASE_SPEED - 450;
        if (right_pwm < 0) right_pwm = 0;
        if (left_pwm > 4320) left_pwm = 4320;
        Set_Motor(left_pwm, right_pwm);
        delay_ms(60);
        return 1;
    }
    // 情况3：只有最左（S1）黑线 -> 更剧烈的左转（近原地）
    if (sensor[0] && !sensor[1] && !sensor[2] && !sensor[3] && !sensor[4]) {
        Set_Motor(-BASE_SPEED, BASE_SPEED);
        uint32_t timeout = HAL_GetTick() + 1500;
        while (HAL_GetTick() < timeout) {
            uint8_t tmp[5];
            Read_Sensors(tmp);
            if (tmp[2]) break;
            delay_ms(10);
        }
        Set_Motor(0,0);
        delay_ms(30);
        return 1;
    }
    // 情况4：只有最右（S5）黑线 -> 原地右转
    if (!sensor[0] && !sensor[1] && !sensor[2] && !sensor[3] && sensor[4]) {
        Set_Motor(BASE_SPEED, -BASE_SPEED);
        uint32_t timeout = HAL_GetTick() + 1500;
        while (HAL_GetTick() < timeout) {
            uint8_t tmp[5];
            Read_Sensors(tmp);
            if (tmp[2]) break;
            delay_ms(10);
        }
        Set_Motor(0,0);
        delay_ms(30);
        return 1;
    }
    return 0;
}

// 丢线恢复：边前进边转弯搜索黑线（圆弧）
static uint8_t Handle_Lost_Line(uint8_t *sensor, int8_t last_dir)
{
    if (!sensor[0] && !sensor[1] && !sensor[2] && !sensor[3] && !sensor[4]) {
        Set_Motor(0,0);
        delay_ms(30);
        
        int16_t fwd_speed = (int16_t)(BASE_SPEED * LOST_LINE_FWD_RATIO);
        int16_t turn_pwm = 250;
        int16_t left_pwm, right_pwm;
        if (last_dir < 0) {
            left_pwm = fwd_speed - turn_pwm;
            right_pwm = fwd_speed + turn_pwm;
        } else {
            left_pwm = fwd_speed + turn_pwm;
            right_pwm = fwd_speed - turn_pwm;
        }
        if (left_pwm < -4320) left_pwm = -4320;
        if (left_pwm > 4320) left_pwm = 4320;
        if (right_pwm < -4320) right_pwm = -4320;
        if (right_pwm > 4320) right_pwm = 4320;
        
        uint32_t start_time = HAL_GetTick();
        uint8_t found = 0;
        while ((HAL_GetTick() - start_time) < 3000) {
            Set_Motor(left_pwm, right_pwm);
            delay_ms(20);
            uint8_t tmp[5];
            Read_Sensors(tmp);
            if (tmp[2]) {
                found = 1;
                break;
            }
        }
        Set_Motor(0,0);
        if (found) {
            delay_ms(50);
            return 1;
        } else {
            char buf[64];
            sprintf(buf, "[ERROR] Lost line, stop\r\n");
            HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 100);
            return 1;
        }
    }
    return 0;
}

// 巡线主任务
void Line_Follow_Task(void)
{
    static uint32_t last_time = 0;
    static int8_t last_dir = 0;
    uint32_t now = HAL_GetTick();
    if (now - last_time < 20) return;
    last_time = now;

    uint8_t sensors[5];
    Read_Sensors(sensors);

    // 1. 丢线恢复
    if (Handle_Lost_Line(sensors, last_dir)) {
        return;
    }

    // 2. 边缘转弯
    if (Handle_Edge_Turn(sensors)) {
        if (sensors[0] || (sensors[0]&&sensors[1])) last_dir = -1;
        else if (sensors[4] || (sensors[3]&&sensors[4])) last_dir = 1;
        return;
    }

    // 3. 正常巡线：计算误差
    float error = Get_Position_Error(sensors);
    
    // 更新 last_dir（用于丢线恢复）
    if (error < -0.5f) last_dir = -1;
    else if (error > 0.5f) last_dir = 1;
    
    // ---------- 方向保持逻辑（防止弯道误转向） ----------
    // 根据误差符号判断当前趋势
    int8_t current_trend = 0;
    if (error < -0.5f) current_trend = -1;
    else if (error > 0.5f) current_trend = 1;
    
    // 更新 persistent_dir 和 persistent_cnt
    if (current_trend != 0) {
        if (current_trend == persistent_dir) {
            if (persistent_cnt < HOLD_THRESHOLD) persistent_cnt++;
        } else {
            // 方向相反，减少计数但不清零
            if (persistent_cnt > 0) persistent_cnt--;
            if (persistent_cnt == 0) persistent_dir = 0;
        }
        // 如果 persistent_dir 为0，则设置为当前趋势
        if (persistent_dir == 0) {
            persistent_dir = current_trend;
            persistent_cnt = 1;
        }
    } else {
        // 误差很小，逐渐释放锁定
        if (persistent_cnt > 0) persistent_cnt--;
        if (persistent_cnt == 0) persistent_dir = 0;
    }
    
    // 如果锁定生效，强制将误差设为一个小值（方向与锁定方向一致）
    float final_error = error;
    if (persistent_cnt >= HOLD_THRESHOLD && persistent_dir != 0) {
        // 如果当前误差方向与锁定方向相反，则强制用小值替代（不反转）
        if ((error > 0 && persistent_dir == -1) || (error < 0 && persistent_dir == 1)) {
            final_error = (persistent_dir == -1) ? -0.5f : 0.5f;
        } else {
            final_error = error;  // 方向一致，保持原误差
        }
    }
    // ------------------------------------------------
    
    // PID 计算（使用 final_error 作为比例输入，积分仍用原始 error 避免饱和）
    if (fabs(error) < 1.5f && (sensors[0]||sensors[1]||sensors[2]||sensors[3]||sensors[4])) {
        pid.integral += error * 0.02f;
        if (pid.integral > pid.integral_limit) pid.integral = pid.integral_limit;
        if (pid.integral < -pid.integral_limit) pid.integral = -pid.integral_limit;
    } else {
        pid.integral = 0;
    }
    
    float p = pid.Kp * final_error;
    float i = pid.Ki * pid.integral;
    float d = pid.Kd * (error - pid.last_error) / 0.02f;
    pid.last_error = error;
    
    float steering = p + i + d;
    if (steering > MAX_STEERING) steering = MAX_STEERING;
    if (steering < -MAX_STEERING) steering = -MAX_STEERING;
    
    int16_t left_pwm = BASE_SPEED + steering;
    int16_t right_pwm = BASE_SPEED - steering;
    if (left_pwm > 4320) left_pwm = 4320;
    if (left_pwm < -4320) left_pwm = -4320;
    if (right_pwm > 4320) right_pwm = 4320;
    if (right_pwm < -4320) right_pwm = -4320;
    
    Set_Motor(left_pwm, right_pwm);
    
    // 调试打印
    static uint32_t last_print = 0;
    if (now - last_print > 200) {
        char buf[80];
        sprintf(buf, "S:%d%d%d%d%d Err:%.2f Steer:%.1f Hold:%d\r\n",
                sensors[0],sensors[1],sensors[2],sensors[3],sensors[4],
                error, steering, persistent_dir);
        HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 100);
        last_print = now;
    }
}

void Line_Follow_Init(void)
{
    pid.integral = 0;
    pid.last_error = 0;
    persistent_dir = 0;
    persistent_cnt = 0;
}