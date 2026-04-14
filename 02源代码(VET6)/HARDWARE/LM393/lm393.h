#ifndef __LM393_H
#define __LM393_H
#include "sys.h"
/*
	GND   电源地
    VCC   接3.3v电源
    A0    接PA4
    D0    接PB7
*/
#define FIRE_D0 PBout(7)

//火焰传感器初始化
extern void lm393_init(void);
extern uint8_t fire_get_d0(void);

#endif

