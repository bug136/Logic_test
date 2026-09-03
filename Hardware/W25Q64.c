#include "W25Q64.h"
#include "Delay.h"

/*
 * STM32F103 SPI1 无重映射：
 *   PA4  软件 CS（空闲高，通信时拉低）
 *   PA5  SCK
 *   PA6  MISO  ← 接 W25Q64 DO
 *   PA7  MOSI  → 接 W25Q64 DI
 *
 * W25Q64BV 只支持 SPI Mode 0 / Mode 3。
 * 时钟沿：DI 上升沿采样，DO 下降沿移出。本驱动用 Mode 0。
 */

#define W25Q64_CS_LOW()   GPIO_ResetBits(GPIOA, GPIO_Pin_4)
#define W25Q64_CS_HIGH()  GPIO_SetBits(GPIOA, GPIO_Pin_4)

static uint8_t W25Q64_SwapByte(uint8_t data)
{
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
	SPI_I2S_SendData(SPI1, data);
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
	return (uint8_t)SPI_I2S_ReceiveData(SPI1);
}

static void W25Q64_EndFrame(void)
{
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
	W25Q64_CS_HIGH();
}

void W25Q64_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;     /* 软件 CS，空闲必须为高 */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	W25Q64_CS_HIGH();

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;      /* SCK、MOSI */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;        /* MISO，接 W25Q64 DO */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	SPI_InitTypeDef SPI_InitStructure;
	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;           /* Mode 0：空闲时钟低 */
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;         /* Mode 0：第 1 沿采样 */
	SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_256; /* ≈281kHz，方便分析仪 */
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial = 7;
	SPI_Init(SPI1, &SPI_InitStructure);
	SPI_Cmd(SPI1, ENABLE);
}

/**
 * JEDEC ID（手册 9Fh）
 *   MOSI：9Fh
 *   MISO：MF7-MF0，ID15-ID8，ID7-ID0
 * W25Q64BV 期望：id1=0xEF，id2=0x4017
 *
 * 示例里 HAL_SPI_Transmit(&hspi1, 0x9F, ...) 把立即数当指针用了，
 * 标准库这里用交换字节，命令值真正发出去。
 */
void W25Q64_ReadID(uint8_t *id1, uint16_t *id2)
{
	uint8_t id_h;
	uint8_t id_l;

	W25Q64_CS_LOW();                         /* CS 拉低，开始一帧（对应 GPIO_PIN_RESET） */
	W25Q64_SwapByte(W25Q64_CMD_JEDEC_ID);    /* 发 0x9F，此时回读丢掉 */
	*id1 = W25Q64_SwapByte(0xFF);            /* Manufacturer ID */
	id_h = W25Q64_SwapByte(0xFF);            /* Memory Type */
	id_l = W25Q64_SwapByte(0xFF);            /* Capacity */
	*id2 = ((uint16_t)id_h << 8) | id_l;
	W25Q64_EndFrame();                       /* CS 拉高，指令才结束 */
}

uint8_t W25Q64_ReadStatus(void)
{
	uint8_t status;

	W25Q64_CS_LOW();
	W25Q64_SwapByte(W25Q64_CMD_READ_STATUS1);    /* 05h */
	status = W25Q64_SwapByte(0xFF);
	W25Q64_EndFrame();
	return status;
}

void W25Q64_WriteEnable(void)
{
	W25Q64_CS_LOW();
	W25Q64_SwapByte(W25Q64_CMD_WRITE_ENABLE);    /* 06h，写/擦之前必须发 */
	W25Q64_EndFrame();
}

/* 轮询 Status-1 的 BUSY 位；擦除最长约几百毫秒，超时返回 1 */
uint8_t W25Q64_WaitBusy(void)
{
	uint32_t timeout = 500000;

	while (W25Q64_ReadStatus() & W25Q64_STATUS_BUSY)
	{
		if (--timeout == 0)
		{
			return 1;
		}
		Delay_us(10);
	}
	return 0;
}

void W25Q64_SectorErase(uint32_t addr)
{
	W25Q64_WriteEnable();
	W25Q64_CS_LOW();
	W25Q64_SwapByte(W25Q64_CMD_SECTOR_ERASE);    /* 20h + 24 位地址 */
	W25Q64_SwapByte((uint8_t)(addr >> 16));
	W25Q64_SwapByte((uint8_t)(addr >> 8));
	W25Q64_SwapByte((uint8_t)addr);
	W25Q64_EndFrame();
	W25Q64_WaitBusy();
}

/* 同一页内编程，len 不要超过 256，也不要跨页 */
void W25Q64_PageProgram(uint32_t addr, uint8_t *data, uint16_t len)
{
	uint16_t i;

	if (len == 0)
	{
		return;
	}
	if (len > W25Q64_PAGE_SIZE)
	{
		len = W25Q64_PAGE_SIZE;
	}

	W25Q64_WriteEnable();
	W25Q64_CS_LOW();
	W25Q64_SwapByte(W25Q64_CMD_PAGE_PROGRAM);    /* 02h + 24 位地址 + 数据 */
	W25Q64_SwapByte((uint8_t)(addr >> 16));
	W25Q64_SwapByte((uint8_t)(addr >> 8));
	W25Q64_SwapByte((uint8_t)addr);
	for (i = 0; i < len; i++)
	{
		W25Q64_SwapByte(data[i]);
	}
	W25Q64_EndFrame();
	W25Q64_WaitBusy();
}

void W25Q64_ReadData(uint32_t addr, uint8_t *data, uint16_t len)
{
	uint16_t i;

	W25Q64_CS_LOW();
	W25Q64_SwapByte(W25Q64_CMD_READ_DATA);       /* 03h + 24 位地址 + 连续读 */
	W25Q64_SwapByte((uint8_t)(addr >> 16));
	W25Q64_SwapByte((uint8_t)(addr >> 8));
	W25Q64_SwapByte((uint8_t)addr);
	for (i = 0; i < len; i++)
	{
		data[i] = W25Q64_SwapByte(0xFF);
	}
	W25Q64_EndFrame();
}
