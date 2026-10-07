#ifndef __UART_H__
#define __UART_H__

#include "stm32f1xx_hal.h"

void UART_Rx_Feed(const uint8_t *data, uint16_t len);

#endif