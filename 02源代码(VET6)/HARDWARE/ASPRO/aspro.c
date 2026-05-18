/**
 * @file    aspro.c
 * @brief   STM32 与 ASRPRO 语音模块（USART2）通信驱动
 *
 * @details
 * 硬件：USART2（PA2=TX → 模块 RX，PA3=RX ← 模块 TX），波特率 9600。
 * 协议与「03智慧家居语音工程/asrpro_project.c」中 task_serial() 一致：
 *   - 火警主动/查询：3#=安全，4#=危险
 *   - 烟雾主动/查询：5#=安全，6#=危险
 *   - 温湿度：模块先识别 TEMP/HUMI 后，MCU 仅下发数值，如 25#、70#
 *
 * @note 报文以 '#' 结尾；模块侧 readStringUntil('#') 解析。
 */

#include "includes.h"

/**
 * @brief 初始化语音模块串口及 PA8（模块状态脚，输入）
 * @param baud 串口波特率，须与 ASRPRO 工程 Serial2.begin() 一致（本工程为 9600）
 */
void asr_init(uint32_t baud)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	usart2_init(baud);

	/* PA8：ASRPRO 工作状态指示（模块 PA3 输出，见 asrpro_project.c digitalWrite(3,…)） */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}

/**
 * @brief 向 ASRPRO 发送以 '#' 结尾的 ASCII 指令
 * @param str 指令字符串，如 "4#"、"70#"
 */
void asr_send_str(char *str)
{
	if(str == NULL || str[0] == '\0')
		return;

	dgb_printf_safe("[asr_tx] %s\r\n", str);
	usart_send_str(USART2, str);
}

/**
 * @brief 通知火警状态（主动报警或应答 FIRE# 查询）
 * @param danger 0=安全 → 发 3#；非 0=危险 → 发 4#
 * @note 模块将播「当前火警状态」+「安全/不安全」
 */
void asr_notify_fire(uint8_t danger)
{
	char buf[8];

	sprintf(buf, "%u#", danger ? ASR_VAL_FIRE_DANGER : ASR_VAL_FIRE_SAFE);
	asr_send_str(buf);
}

/**
 * @brief 通知烟雾状态（主动报警或应答 GAS# 查询）
 * @param danger 0=安全 → 发 5#；非 0=危险 → 发 6#
 * @note 模块将播「当前烟雾报警状态」+「安全/不安全」
 */
void asr_notify_gas(uint8_t danger)
{
	char buf[8];

	sprintf(buf, "%u#", danger ? ASR_VAL_GAS_DANGER : ASR_VAL_GAS_SAFE);
	asr_send_str(buf);
}

/**
 * @brief 播报温度整数部分（须在模块已进入 ASR_ID_TEMP_GET 后调用）
 * @param temp_int DHT11 温度整数，如 25 → 发送 "25#"
 */
void asr_play_temp(uint8_t temp_int)
{
	char buf[8];

	sprintf(buf, "%u#", (unsigned)temp_int);
	asr_send_str(buf);
}

/**
 * @brief 播报湿度整数部分（须在模块已进入 ASR_ID_HUMI_GET 后调用）
 * @param humi_int DHT11 湿度整数，如 70 → 发送 "70#"
 */
void asr_play_humi(uint8_t humi_int)
{
	char buf[8];

	sprintf(buf, "%u#", (unsigned)humi_int);
	asr_send_str(buf);
}
