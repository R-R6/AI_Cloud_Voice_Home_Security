#ifndef __BEEP_H
#define __BEEP_H
#include "sys.h"

#define BEEP PEout(1)	//蜂鸣器

////定时器3初始化
//extern void tim13_init(void);

////设置定时器13的PWM频率
//extern void tim13_set_freq(uint32_t freq);

////设置定时器13的PWM占空比0%~100%
//extern void tim13_set_duty(uint32_t duty);

//蜂鸣器初始化
extern void beep_init(void);

#endif
