#ifndef __W25Q64_H
#define __W25Q64_H

#include "stm32f10x.h"

/*
 * W25Q64BV 指令（手册 Table 11.2.2 Instruction Set）
 * SPI Mode 0 或 Mode 3，MSB 先发，CS 低有效
 */
#define W25Q64_CMD_WRITE_ENABLE      0x06
#define W25Q64_CMD_WRITE_DISABLE     0x04
#define W25Q64_CMD_READ_STATUS1      0x05
#define W25Q64_CMD_READ_STATUS2      0x35
#define W25Q64_CMD_JEDEC_ID          0x9F    /* 回 3 字节：MF、Memory Type、Capacity */
#define W25Q64_CMD_READ_DATA         0x03
#define W25Q64_CMD_PAGE_PROGRAM      0x02
#define W25Q64_CMD_SECTOR_ERASE      0x20    /* 4KB */
#define W25Q64_CMD_CHIP_ERASE        0xC7

/* JEDEC ID：Winbond 0xEF，W25Q64 为 Memory Type=0x40、Capacity=0x17 */
#define W25Q64_MANUFACTURER_ID       0xEF
#define W25Q64_DEVICE_ID             0x4017

#define W25Q64_PAGE_SIZE             256
#define W25Q64_SECTOR_SIZE           4096
#define W25Q64_STATUS_BUSY           0x01    /* Status Register-1 bit0 */

void W25Q64_Init(void);

/* 仿示例：id1=厂商 ID，id2=器件 ID（高字节 Memory Type，低字节 Capacity） */
void W25Q64_ReadID(uint8_t *id1, uint16_t *id2);

uint8_t W25Q64_ReadStatus(void);
void W25Q64_WriteEnable(void);
uint8_t W25Q64_WaitBusy(void);
void W25Q64_SectorErase(uint32_t addr);
void W25Q64_PageProgram(uint32_t addr, uint8_t *data, uint16_t len);
void W25Q64_ReadData(uint32_t addr, uint8_t *data, uint16_t len);

#endif
