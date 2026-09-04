#ifndef __M24C08_H
#define __M24C08_H

#include "stm32f10x.h"

/*
 * Microchip 24C08B：8Kbit = 4 × 256 字节
 * 控制字节：1 0 1 0  B2 B1 B0  R/W
 *   块 0 写地址 0xA0，读地址 0xA1（与示例 M24C02 相同）
 * 片内字地址 8 位；块号放在控制字节的 B1、B0
 */
#define M24C08_ADDR_WRITE   0xA0
#define M24C08_ADDR_READ    0xA1
#define M24C08_PAGE_SIZE    16
#define M24C08_SIZE         1024

void M24C08_Init(void);

/* 等价于 HAL_I2C_Mem_Write(..., I2C_MEMADD_SIZE_8BIT, ...) */
uint8_t M24C08_Mem_Write(uint8_t dev_write_addr, uint8_t mem_addr,
                         uint8_t *data, uint16_t len);

/* 随机读：先写字地址，再 Restart 读 */
uint8_t M24C08_Mem_Read(uint8_t dev_write_addr, uint8_t mem_addr,
                        uint8_t *data, uint16_t len);

#endif
