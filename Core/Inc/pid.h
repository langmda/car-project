#ifndef _PID_H
#define _PID_H

#include "stm32f1xx_hal.h"

#pragma pack(1)  // 强制1字节对齐
typedef struct {
    uint8_t cmd;
    float Kp;
    float Ki;
    int32_t I_Lim;
    int32_t PWM_Lim;
} PIDParams_t;
#pragma pack()   // 恢复默认对齐

void PI_Param_Clear(void);
void Read_Encoder(void);
float PID_Calculate(void);
void PID_Go_Straight(int16_t base_pwm);
void Debug_Print(const char* format, ...);
void Print_Debug(void); 
void PID_Reset_Yaw_Reference(void);

extern volatile int16_t encoder_left, encoder_right;
extern volatile int32_t total_encoder_left;
extern volatile int32_t total_encoder_right;

extern float Kp;
extern float Ki;
extern uint16_t I_Limit;		
extern uint16_t PWM_Limit; 
extern int16_t last_left, last_right;

void PID_Go_Straight_Continuous(int16_t base_pwm);
void PID_Start_Continuous(int16_t base_pwm);
void PID_Stop_Continuous(void);
#endif
