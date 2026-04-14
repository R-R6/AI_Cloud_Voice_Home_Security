#ifndef __MQ2_H
#define __MQ2_H
#include "sys.h"

/*
	MQ-2 烟雾传感器
	VCC   接5V
	GND   接地
	DO    接PA1（数字开关量输出）
	A0    不使用
*/

// 传感器初始化
extern void mq2_init(void);
// 读取DO引脚状态
extern uint8_t mq2_get_do(void);

#endif
