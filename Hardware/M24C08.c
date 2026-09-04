#include "M24C08.h"
#include "Delay.h"

/*
 * I2C1 无重映射：
 *   PB6  SCL  ← 24C08 SCL
 *   PB7  SDA  ← 24C08 SDA
 * 手册：100 kHz、写周期最大 10ms、页写最多 16 字节
 *
 * 字节写：START → 控制字节(写) → 字地址 → 数据 → STOP
 * 随机读：START → 控制字节(写) → 字地址 → Restart → 控制字节(读) → 数据 → STOP
 */

static uint8_t M24C08_WaitEvent(uint32_t event)
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

static uint8_t M24C08_WaitFlag(uint32_t flag, FlagStatus status)
{
	uint32_t timeout = 10000;

	while (I2C_GetFlagStatus(I2C1, flag) != status)
	{
		if (--timeout == 0)
		{
			return 0;
		}
	}
	return 1;
}

static void M24C08_Stop(void)
{
	I2C_GenerateSTOP(I2C1, ENABLE);
}

/* 发 START，等 EV5（主模式） */
static uint8_t M24C08_Start(void)
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
	return M24C08_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT);
}

static uint8_t M24C08_Restart(void)
{
	I2C_GenerateSTART(I2C1, ENABLE);
	return M24C08_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT);
}

static uint8_t M24C08_SendByte(uint8_t data)
{
	I2C_SendData(I2C1, data);
	return M24C08_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED);
}

void M24C08_Init(void)
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
	I2C_InitStructure.I2C_ClockSpeed = 100000;   /* 手册 FCLK 最大 100 kHz */
	I2C_Init(I2C1, &I2C_InitStructure);

	I2C_Cmd(I2C1, ENABLE);
	I2C_AcknowledgeConfig(I2C1, ENABLE);
}

uint8_t M24C08_Mem_Write(uint8_t dev_write_addr, uint8_t mem_addr,
                         uint8_t *data, uint16_t len)
{
	uint16_t i;

	if ((len == 0) || (len > M24C08_PAGE_SIZE))
	{
		return 0;
	}

	if (!M24C08_Start())
	{
		return 0;
	}

	I2C_Send7bitAddress(I2C1, dev_write_addr, I2C_Direction_Transmitter);
	if (!M24C08_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
	{
		M24C08_Stop();
		return 0;
	}

	if (!M24C08_SendByte(mem_addr))
	{
		M24C08_Stop();
		return 0;
	}

	for (i = 0; i < len; i++)
	{
		if (!M24C08_SendByte(data[i]))
		{
			M24C08_Stop();
			return 0;
		}
	}

	M24C08_Stop();
	Delay_ms(10);    /* 手册写周期 TWR 最大 10 ms，期间芯片不应答 */
	return 1;
}

uint8_t M24C08_Mem_Read(uint8_t dev_write_addr, uint8_t mem_addr,
                        uint8_t *data, uint16_t len)
{
	uint16_t i;

	if (len == 0)
	{
		return 0;
	}

	if (!M24C08_Start())
	{
		return 0;
	}

	I2C_Send7bitAddress(I2C1, dev_write_addr, I2C_Direction_Transmitter);
	if (!M24C08_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
	{
		M24C08_Stop();
		return 0;
	}

	if (!M24C08_SendByte(mem_addr))
	{
		M24C08_Stop();
		return 0;
	}

	if (!M24C08_Restart())
	{
		M24C08_Stop();
		return 0;
	}

	I2C_Send7bitAddress(I2C1, dev_write_addr, I2C_Direction_Receiver);

	if (len == 1)
	{
		if (!M24C08_WaitFlag(I2C_FLAG_ADDR, SET))
		{
			M24C08_Stop();
			return 0;
		}
		I2C_AcknowledgeConfig(I2C1, DISABLE);
		(void)I2C1->SR2;
		M24C08_Stop();
		if (!M24C08_WaitFlag(I2C_FLAG_RXNE, SET))
		{
			I2C_AcknowledgeConfig(I2C1, ENABLE);
			return 0;
		}
		data[0] = I2C_ReceiveData(I2C1);
		I2C_AcknowledgeConfig(I2C1, ENABLE);
		return 1;
	}

	if (!M24C08_WaitEvent(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED))
	{
		M24C08_Stop();
		return 0;
	}

	I2C_AcknowledgeConfig(I2C1, ENABLE);
	for (i = 0; i < (len - 2); i++)
	{
		if (!M24C08_WaitEvent(I2C_EVENT_MASTER_BYTE_RECEIVED))
		{
			M24C08_Stop();
			return 0;
		}
		data[i] = I2C_ReceiveData(I2C1);
	}

	/*
	 * 倒数第 2 字节到达后：先关 ACK、再发 STOP，最后才读 DR。
	 * STM32 读 DR 会立刻再打一组时钟；若此时 STOP 还没挂上，
	 * 总线上就会多出一字节 0xFF（SDA 已释放为高）。
	 */
	if (!M24C08_WaitEvent(I2C_EVENT_MASTER_BYTE_RECEIVED))
	{
		M24C08_Stop();
		return 0;
	}
	I2C_AcknowledgeConfig(I2C1, DISABLE);
	M24C08_Stop();
	data[len - 2] = I2C_ReceiveData(I2C1);

	if (!M24C08_WaitEvent(I2C_EVENT_MASTER_BYTE_RECEIVED))
	{
		I2C_AcknowledgeConfig(I2C1, ENABLE);
		return 0;
	}
	data[len - 1] = I2C_ReceiveData(I2C1);
	I2C_AcknowledgeConfig(I2C1, ENABLE);
	return 1;
}
