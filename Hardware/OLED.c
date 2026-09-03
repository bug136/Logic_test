#include "OLED.h"
#include "Delay.h"

/*
 * SSD1306 I2C 写协议（逻辑分析仪解码时对照）：
 *   START → 地址 0x3C Write(0x78) → ACK
 *   → 控制字节：0x00=命令 / 0x40=显示数据 → ACK
 *   → 载荷字节… → STOP
 *
 * 接线：PB6=SCL，PB7=SDA，GND/3.3V；多数模块板载已有上拉。
 */

/* 'a' 的 8x16 点阵：先 8 字节上半页，再 8 字节下半页（列优先，每字节 8 行） */
static const uint8_t Font_A_8x16[16] = {
	0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00,
	0x00, 0x19, 0x24, 0x22, 0x22, 0x3F, 0x20, 0x00
};

static uint8_t OLED_I2C_WaitEvent(uint32_t event)
{
	uint32_t timeout = 10000;
	while (I2C_CheckEvent(I2C1, event) != SUCCESS)
	{
		if (I2C_GetFlagStatus(I2C1, I2C_FLAG_AF) == SET)
		{
			I2C_ClearFlag(I2C1, I2C_FLAG_AF);
			return 0;
		}
		if (--timeout == 0)
		{
			return 0;
		}
	}
	return 1;
}

static uint8_t OLED_I2C_Start(void)
{
	uint32_t timeout = 10000;
	while (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY) == SET)
	{
		if (--timeout == 0)
		{
			I2C_GenerateSTOP(I2C1, ENABLE);
			return 0;
		}
	}

	I2C_GenerateSTART(I2C1, ENABLE);
	if (!OLED_I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT))
	{
		I2C_GenerateSTOP(I2C1, ENABLE);
		return 0;
	}

	I2C_Send7bitAddress(I2C1, (OLED_I2C_ADDR7 << 1), I2C_Direction_Transmitter);
	if (!OLED_I2C_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
	{
		I2C_GenerateSTOP(I2C1, ENABLE);
		return 0;
	}
	return 1;
}

static void OLED_I2C_Stop(void)
{
	I2C_GenerateSTOP(I2C1, ENABLE);
}

static uint8_t OLED_I2C_SendByte(uint8_t data)
{
	I2C_SendData(I2C1, data);
	return OLED_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED);
}

/* 写一条命令：控制字节 0x00 + 命令 */
static void OLED_WriteCmd(uint8_t cmd)
{
	if (!OLED_I2C_Start())
	{
		return;
	}
	if (!OLED_I2C_SendByte(0x00))
	{
		OLED_I2C_Stop();
		return;
	}
	OLED_I2C_SendByte(cmd);
	OLED_I2C_Stop();
}

/* 写一字节显存数据：控制字节 0x40 + 数据 */
static void OLED_WriteData(uint8_t data)
{
	if (!OLED_I2C_Start())
	{
		return;
	}
	if (!OLED_I2C_SendByte(0x40))
	{
		OLED_I2C_Stop();
		return;
	}
	OLED_I2C_SendByte(data);
	OLED_I2C_Stop();
}

/* 连续写多字节显存（一次 START，控制字节 0x40，再跟一串数据） */
static void OLED_WriteDataBurst(const uint8_t *buf, uint16_t len)
{
	uint16_t i;

	if (!OLED_I2C_Start())
	{
		return;
	}
	if (!OLED_I2C_SendByte(0x40))
	{
		OLED_I2C_Stop();
		return;
	}
	for (i = 0; i < len; i++)
	{
		if (!OLED_I2C_SendByte(buf[i]))
		{
			break;
		}
	}
	OLED_I2C_Stop();
}

static void OLED_SetPos(uint8_t x, uint8_t page)
{
	OLED_WriteCmd(0xB0 | (page & 0x07));
	OLED_WriteCmd(0x10 | ((x >> 4) & 0x0F));
	OLED_WriteCmd(0x00 | (x & 0x0F));
}

static void OLED_I2C_GPIO_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	I2C_InitTypeDef I2C_InitStructure;
	I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
	I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
	I2C_InitStructure.I2C_OwnAddress1 = 0x00;
	I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
	I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
	I2C_InitStructure.I2C_ClockSpeed = 100000;
	I2C_Init(I2C1, &I2C_InitStructure);

	I2C_Cmd(I2C1, ENABLE);
}

void OLED_Clear(void)
{
	uint8_t page;
	uint8_t zeros[128];
	uint8_t i;

	for (i = 0; i < 128; i++)
	{
		zeros[i] = 0x00;
	}

	for (page = 0; page < 8; page++)
	{
		OLED_SetPos(0, page);
		OLED_WriteDataBurst(zeros, 128);
	}
}

void OLED_ShowChar(uint8_t x, uint8_t y, char ch)
{
	/* y：页号 0~7；8x16 字占两页。目前只实现 'a' */
	if (ch != 'a' && ch != 'A')
	{
		return;
	}

	OLED_SetPos(x, y);
	OLED_WriteDataBurst(&Font_A_8x16[0], 8);
	OLED_SetPos(x, (uint8_t)(y + 1));
	OLED_WriteDataBurst(&Font_A_8x16[8], 8);
}

void OLED_WriteCharA(void)
{
	OLED_ShowChar(0, 0, 'a');
}

void OLED_Init(void)
{
	OLED_I2C_GPIO_Init();
	Delay_ms(100);

	OLED_WriteCmd(0xAE); /* 关显示 */
	OLED_WriteCmd(0xD5);
	OLED_WriteCmd(0x80);
	OLED_WriteCmd(0xA8);
	OLED_WriteCmd(0x3F); /* 1/64 duty，128x64 */
	OLED_WriteCmd(0xD3);
	OLED_WriteCmd(0x00);
	OLED_WriteCmd(0x40);
	OLED_WriteCmd(0x8D);
	OLED_WriteCmd(0x14); /* 内部电荷泵开 */
	OLED_WriteCmd(0x20);
	OLED_WriteCmd(0x02); /* 页寻址 */
	OLED_WriteCmd(0xA1); /* 段重映射 */
	OLED_WriteCmd(0xC8); /* COM 扫描方向 */
	OLED_WriteCmd(0xDA);
	OLED_WriteCmd(0x12);
	OLED_WriteCmd(0x81);
	OLED_WriteCmd(0xCF);
	OLED_WriteCmd(0xD9);
	OLED_WriteCmd(0xF1);
	OLED_WriteCmd(0xDB);
	OLED_WriteCmd(0x40);
	OLED_WriteCmd(0xA4);
	OLED_WriteCmd(0xA6); /* 正常显示（非反色） */
	OLED_WriteCmd(0xAF); /* 开显示 */

	OLED_Clear();
}
