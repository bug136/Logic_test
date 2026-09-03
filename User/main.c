 #include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "W25Q64.h"

/*
 * 逻辑分析仪波形演示（STM32F103C8，无重映射）
 *
 * OLED 0.9寸 SSD1306（本步先做 I2C 写）：
 *   PB6  I2C1_SCL   接 OLED SCL
 *   PB7  I2C1_SDA   接 OLED SDA
 *   7位地址 0x3C，写地址 0x78；写命令控制字节 0x00，写显存控制字节 0x40
 *   上电初始化后，周期性向左上角写入字符 'a'，便于抓写波形
 *
 * SPI1 ↔ W25Q64BV（3.3V，Mode 0）：
 *   PA4  CS    ↔ 芯片 /CS
 *   PA5  SCK   ↔ 芯片 CLK
 *   PA6  MISO  ↔ 芯片 DO
 *   PA7  MOSI  ↔ 芯片 DI
 *   3.3V/GND   ↔ VCC/GND；/WP、/HOLD 接 3.3V
 *
 * 其它引脚：
 *   PA0  TIM2_CH1   每 10ms 翻转
 *   PA1  TIM2_CH2   PWM 25%
 *   PA9 / PA10      串口
 */
#define USART_RX_BUF_SIZE 32

static volatile uint8_t USART1_RxBuf[USART_RX_BUF_SIZE];
static volatile uint8_t USART1_RxWrite = 0;
static volatile uint8_t USART1_RxRead = 0;

static void Timer_PWM_Init(void);
static void USART_InitConfig(void);

static void USART1_SendByte(uint8_t data);
static void USART1_SendString(char *str);
static void USART1_SendHex8(uint8_t data);
static void USART2_SendByte(uint8_t data);
static void USART2_SendRxTest(void);
static void USART1_EchoReceived(void);
static void W25Q64_DemoOnce(void);
static void W25Q64_PrintID(uint8_t id1, uint16_t id2);

int main(void)
{
	uint8_t id1;
	uint16_t id2;

	Timer_PWM_Init();
	USART_InitConfig();
	W25Q64_Init();            /* SPI1 + 软件 CS */
	OLED_Init();              /* I2C + SSD1306 初始化、清屏 */

	W25Q64_DemoOnce();        /* 上电：读 ID，匹配则擦写读校验一次 */

	while (1)
	{
		OLED_WriteCharA();    /* I2C 写：左上角显示 'a'（分析仪看 PB6/PB7） */
		USART1_SendString("U STM32 UART\r\n");
		USART2_SendRxTest();
		Delay_us(200);
		USART1_EchoReceived();

		W25Q64_ReadID(&id1, &id2);   /* CS 低 → 9F → 回 3 字节 → CS 高 */
		W25Q64_PrintID(id1, id2);

		Delay_ms(200);        /* 间隔稍大，方便单独抓 OLED / SPI 波形 */
	}
}

/**
 * TIM2：PSC=71 → 计数频率 1MHz
 * ARR=19999 → 周期 20ms（50Hz）
 * CH1(PA0) CCR=10000 → 高电平 10ms、低电平 10ms，每 10ms 翻转一次
 * CH2(PA1) CCR=5000  → 占空比 25% 的 PWM，便于对照
 */
static void Timer_PWM_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	TIM_InternalClockConfig(TIM2);

	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInitStructure.TIM_Period = 20000 - 1;    /* 20ms 溢出一次 */
	TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1;    /* 72MHz/72=1MHz，1 计数 = 1µs */
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

	TIM_OCInitTypeDef TIM_OCInitStructure;
	TIM_OCStructInit(&TIM_OCInitStructure);
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;

	TIM_OCInitStructure.TIM_Pulse = 10000;               /* 高 10ms，然后翻转到低 10ms */
	TIM_OC1Init(TIM2, &TIM_OCInitStructure);
	TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);

	TIM_OCInitStructure.TIM_Pulse = 5000;                /* PWM 25% */
	TIM_OC2Init(TIM2, &TIM_OCInitStructure);
	TIM_OC2PreloadConfig(TIM2, TIM_OCPreload_Enable);

	TIM_ARRPreloadConfig(TIM2, ENABLE);
	TIM_Cmd(TIM2, ENABLE);
}

/**
 * USART1：PA9=TX（发出去的波形），PA10=RX（收进来的波形）
 * USART2：PA2=TX，短接到 PA10，用来给 USART1 制造接收数据
 * 也可用 USB 转串口：模块 TX→PA10、RX→PA9、GND 共地，115200 8N1 发送任意内容
 */
static void USART_InitConfig(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;      /* USART1 TX */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;        /* USART1 RX */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;      /* USART2 TX → 接到 PA10 */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate = 115200;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_Init(USART1, &USART_InitStructure);

	USART_InitStructure.USART_Mode = USART_Mode_Tx;
	USART_Init(USART2, &USART_InitStructure);

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
	USART_Cmd(USART1, ENABLE);
	USART_Cmd(USART2, ENABLE);
}

static void USART1_SendByte(uint8_t data)
{
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
	USART_SendData(USART1, data);
}

static void USART1_SendString(char *str)
{
	while (*str != '\0')
	{
		USART1_SendByte(*str++);
	}
}

static void USART2_SendByte(uint8_t data)
{
	while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
	USART_SendData(USART2, data);
}

static void USART2_SendRxTest(void)
{
	USART2_SendByte(0xAA);     /* 10101010，便于量波特率 */
	USART2_SendByte(0x55);     /* 01010101 */
	USART2_SendByte('R');      /* 0x52 */
	USART2_SendByte('X');      /* 0x58 */
	USART2_SendByte('\r');     /* 0x0D */
	USART2_SendByte('\n');     /* 0x0A */
	while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
}

static void USART1_EchoReceived(void)
{
	while (USART1_RxRead != USART1_RxWrite)
	{
		USART1_SendByte(USART1_RxBuf[USART1_RxRead]);
		USART1_RxRead = (USART1_RxRead + 1) % USART_RX_BUF_SIZE;
	}
}

void USART1_IRQHandler(void)
{
	if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
	{
		uint8_t data = (uint8_t)USART_ReceiveData(USART1);
		uint8_t next = (USART1_RxWrite + 1) % USART_RX_BUF_SIZE;
		if (next != USART1_RxRead)
		{
			USART1_RxBuf[USART1_RxWrite] = data;
			USART1_RxWrite = next;
		}
	}
	if (USART_GetFlagStatus(USART1, USART_FLAG_ORE) == SET)
	{
		USART_ReceiveData(USART1);
	}
}

static void USART1_SendHex8(uint8_t data)
{
	const char *hex = "0123456789ABCDEF";
	USART1_SendByte((uint8_t)hex[data >> 4]);
	USART1_SendByte((uint8_t)hex[data & 0x0F]);
}

static void W25Q64_PrintID(uint8_t id1, uint16_t id2)
{
	USART1_SendString("JEDEC ");
	USART1_SendHex8(id1);
	USART1_SendByte(' ');
	USART1_SendHex8((uint8_t)(id2 >> 8));
	USART1_SendHex8((uint8_t)id2);
	if ((id1 == W25Q64_MANUFACTURER_ID) && (id2 == W25Q64_DEVICE_ID))
	{
		USART1_SendString(" OK\r\n");
	}
	else
	{
		USART1_SendString(" FAIL\r\n");
	}
}

/* 上电做一次：读 JEDEC ID，对上再擦 4KB、写 4 字节、读回核对 */
static void W25Q64_DemoOnce(void)
{
	uint8_t id1;
	uint16_t id2;
	uint8_t wbuf[4] = {0x11, 0x22, 0x33, 0x44};
	uint8_t rbuf[4];
	uint8_t i;
	uint8_t ok;

	W25Q64_ReadID(&id1, &id2);
	W25Q64_PrintID(id1, id2);
	if ((id1 != W25Q64_MANUFACTURER_ID) || (id2 != W25Q64_DEVICE_ID))
	{
		USART1_SendString("W25Q64 not found, skip R/W\r\n");
		return;
	}

	W25Q64_SectorErase(0x000000);
	W25Q64_PageProgram(0x000000, wbuf, 4);
	W25Q64_ReadData(0x000000, rbuf, 4);

	ok = 1;
	USART1_SendString("FLASH ");
	for (i = 0; i < 4; i++)
	{
		USART1_SendHex8(rbuf[i]);
		USART1_SendByte(' ');
		if (rbuf[i] != wbuf[i])
		{
			ok = 0;
		}
	}
	USART1_SendString(ok ? "OK\r\n" : "FAIL\r\n");
}
