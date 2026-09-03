#ifndef __OLED_H
#define __OLED_H

#include "stm32f10x.h"

/* 0.9寸 OLED：128x64，SSD1306，I2C 7位地址常见为 0x3C（写地址 0x78） */
#define OLED_I2C_ADDR7    0x3C

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t x, uint8_t y, char ch);
void OLED_WriteCharA(void);   /* 左上角写一个 'a'，方便分析仪抓 I2C 写波形 */

#endif
