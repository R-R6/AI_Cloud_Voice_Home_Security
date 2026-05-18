#ifndef __ASPRO_H
#define __ASPRO_H

#include <stdint.h>

/*
	GND   电源地
    VCC   接5V电源
    TX    接PA2  (STM32 USART2_TX)
    RX    接PA3  (STM32 USART2_RX)

	与 03智慧家居语音工程/asrpro_project.c 中 task_serial 协议一致：
	- 火警：3#=安全  4#=危险（主动播报，无需先说「当前火警状态」）
	- 烟雾：5#=安全  6#=危险
	- 温湿度：识别 TEMP/HUMI 后 MCU 只发数值，如 25#、70#
*/

#define ASR_VAL_FIRE_SAFE    3u
#define ASR_VAL_FIRE_DANGER  4u
#define ASR_VAL_GAS_SAFE     5u
#define ASR_VAL_GAS_DANGER   6u

extern void asr_init(uint32_t baud);
extern void asr_send_str(char * str);
extern void asr_notify_fire(uint8_t danger);
extern void asr_notify_gas(uint8_t danger);
extern void asr_play_temp(uint8_t temp_int);
extern void asr_play_humi(uint8_t humi_int);

#endif
