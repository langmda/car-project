#include "sensor.h"
#include "tim.h"
#include "dht11.h"
#include "dht22.h"
#include "usart.h"
#include "pid.h"
#include "user.h"
#include "inv_mpu.h"

Frame_t frame;          // 改成用 Frame_t
uint16_t adc_buf[2];

static float last_temp = 0.0f;
static float last_humi = 0.0f;

//crc校验
static uint8_t crc8_update(uint8_t crc, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x07);
            else
                crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void Read_And_Transmit(void)
{
    /* 1. 填帧头与长度 */
    frame.header[0] = FRAME_HEADER_0;
    frame.header[1] = FRAME_HEADER_1;
    frame.len       = sizeof(Payload_t);

    /* 2. 填数据 */
	frame.data.cmd  = SENSOR_DATA;
    frame.data.encoder_left  = encoder_left;
    frame.data.encoder_right = encoder_right;

    uint8_t ret = DHT22_Read_Data(&frame.data.temp, &frame.data.humi);
    if (ret == 0) {
        last_temp = frame.data.temp;
        last_humi = frame.data.humi;
    } else {
        frame.data.temp = last_temp;
        frame.data.humi = last_humi;
    }

    frame.data.pi_adc    = adc_buf[0];
    frame.data.motor_adc = adc_buf[1];

    float pitch, roll;
    if (mpu_dmp_get_data(&pitch, &roll, &frame.data.yaw) == 0) {
        if (frame.data.yaw < 0) {
            frame.data.yaw += 360.0f;
        }
    } else {
        frame.data.yaw = 0.0f;
    }

    uint8_t crc = 0x00;
	crc = crc8_update(crc, (uint8_t *)&frame.len, 2);         // len（小端两字节）
	crc = crc8_update(crc, (uint8_t *)&frame.data, frame.len);// payload
	frame.checksum = crc;

    /* 4. 整帧发送 */
    HAL_UART_Transmit(&huart2, (uint8_t *)&frame, sizeof(Frame_t), 100);
}