#include "uart.h"
#include "usart.h"
#include "user.h"
#include "pid.h"
#include "main.h"
#include "stdio.h"
#include "servo.h"
#include "avoidance.h"
#include "trajectory.h"
#include <string.h>                 // ← 加 memmove / memcpy

volatile uint8_t current_cmd = STOP;
volatile uint8_t led_counter = 0;
extern uint32_t maxpwm, minpwm;

/* ---------- 新增：接收累积缓冲区 ---------- */
#define RX_ACCUM_SIZE   128          // 够放多帧（最大帧 18 字节）
#define FRAME_HEAD0     0xAA
#define FRAME_HEAD1     0x55
#define MAX_PAYLOAD_LEN 32

static uint8_t  rx_accum[RX_ACCUM_SIZE];
static uint16_t rx_accum_len = 0;

/* ---------- 新增：CRC8（与树莓派一致） ---------- */
static uint8_t crc8_update(uint8_t crc, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x07);
            else            crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* ---------- 新增：处理一个已校验的 payload ---------- */
static void handle_command_payload(const uint8_t *payload, uint16_t len)
{
    if (len < 1) return;
    uint8_t cmd = payload[0];

    if (cmd == MODE_SWITCH) {
        Avoidance_Stop();
        if (mode == MANUAL) mode = AUTOMATIC;		//手动模式，切换成避障模式
        else                mode = MANUAL;			//避障模式和循迹模式，切换成手动模式
    }
    else if (cmd == LED_SWITCH) {
        if (led_counter == 0) {
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
            led_counter = 1;
        } else if (led_counter == 1) {
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
            led_counter = 0;
        }
    }
    else if (cmd == PID_PARAM) {
        /* payload[1..] = Kp(4) + Yaw_Kp(4) + PWM_Lim(4) */
        if (len >= 13) {
            PIDParams_t pid;
            memcpy(&pid, payload, 13);
            Kp        = pid.Kp;
            YAW_KP    = pid.YAW_KP;
            PWM_Limit = pid.PWM_Lim;
        }
    }
    else if (cmd == LINE_FOLLOW_MODE_SWITCH) {
        if (mode == LINE_FOLLOW) {
            mode = MANUAL;
            PI_Param_Clear();
        } else {
            mode = LINE_FOLLOW;
            PI_Param_Clear();
        }
    }
    else if (cmd == PWM_UPDATE) {
        /* payload[1..] = rightPWM(4) + leftPWM(4) */
        if (len >= 9) {
            PWMParams PWM;
            memcpy(&PWM, payload, 9);
            leftPWM  = PWM.leftPWM;
            rightPWM = PWM.rightPWM;
        }
    }
    else {
        current_cmd = cmd;    // 普通运动命令
    }
}

/* ---------- 新增：解析累积缓冲区 ---------- */
static void parse_rx_accum(void)
{
    while (rx_accum_len >= 5) {     // 最小帧：AA 55 + len(2) + crc(1) = 5 字节
        /* 1. 找帧头 */
        uint16_t i = 0;
        while (i + 1 < rx_accum_len) {
            if (rx_accum[i] == FRAME_HEAD0 && rx_accum[i+1] == FRAME_HEAD1)
                break;
            i++;
        }
        if (i + 1 >= rx_accum_len) {
            /* 没找到帧头：只保留最后 1 字节 */
            if (rx_accum_len > 1) {
                rx_accum[0] = rx_accum[rx_accum_len - 1];
                rx_accum_len = 1;
            }
            return;
        }
        /* 丢弃帧头前的垃圾 */
        if (i > 0) {
            memmove(rx_accum, rx_accum + i, rx_accum_len - i);
            rx_accum_len -= i;
        }

        /* 2. 长度字段到齐？ */
        if (rx_accum_len < 4) return;   // 半包

        uint16_t payload_len = (uint16_t)rx_accum[2] | ((uint16_t)rx_accum[3] << 8);
        if (payload_len == 0 || payload_len > MAX_PAYLOAD_LEN) {
            /* 非法长度：丢 1 字节重同步 */
            memmove(rx_accum, rx_accum + 1, --rx_accum_len);
            continue;
        }

        uint16_t total_len = 4 + payload_len + 1;
        if (rx_accum_len < total_len) return;   // 半包

        /* 3. CRC 校验（覆盖 len(2) + payload） */
        uint8_t crc_calc = crc8_update(0x00, rx_accum + 2, 2 + payload_len);
        uint8_t crc_recv = rx_accum[total_len - 1];
        if (crc_calc != crc_recv) {
            memmove(rx_accum, rx_accum + 1, --rx_accum_len);
            continue;
        }

        /* 4. 校验通过，处理 payload */
        handle_command_payload(rx_accum + 4, payload_len);

        /* 5. 消费整帧 */
        memmove(rx_accum, rx_accum + total_len, rx_accum_len - total_len);
        rx_accum_len -= total_len;
    }
}

/* ---------- 修改：串口回调 ---------- */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2)
    {
        last_cmd_time = HAL_GetTick();

        /* 1. 追加新收到的数据到累积缓冲区 */
        if (rx_accum_len + Size <= RX_ACCUM_SIZE) {
            memcpy(rx_accum + rx_accum_len,(const uint8_t *)uart_rx_buffer,Size);
            rx_accum_len += Size;
        } else {
            /* 累积缓冲区溢出，清空重来（极少发生） */
            rx_accum_len = 0;
        }

        /* 2. 解析完整帧 */
        parse_rx_accum();

        /* 3. 重启 DMA 接收 */
        HAL_UARTEx_ReceiveToIdle_IT(huart, uart_rx_buffer, sizeof(uart_rx_buffer));
        __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);
    }
}