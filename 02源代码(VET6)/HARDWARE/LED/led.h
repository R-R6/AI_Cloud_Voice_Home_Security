#ifndef __LED_H
#define __LED_H
#include "sys.h"

//LED端口定义
#define LED0 PFout(9)	// LED0
#define LED1 PFout(10)	// LED1
#define LED2 PEout(13)	// LED2
#define LED3 PEout(14)	// LED3

extern void led_init(void);  //led初始化	

#endif
