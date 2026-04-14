#ifndef __SR04_H
#define __SR04_H
#include "sys.h"
#include "delay.h"

/*
	GND   	电源地
    VCC   	接5v电源
    TRIG    接PB6
    ECHO    接PE6
*/
#define SR04_TRIG PBout(6)	//触发工作信号
#define SR04_ECHO PEin(6)	//回响信号

//sr04超声波初始化
extern void sr04_init(void);

//获取超声波距离
extern uint32_t sr04_get_distance(void);
	
#endif
