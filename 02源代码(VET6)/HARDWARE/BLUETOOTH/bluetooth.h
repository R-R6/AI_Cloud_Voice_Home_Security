#ifndef __BLUETOOTH_H
#define __BLUETOOTH_H

// 初始化蓝牙
extern void blue_init(uint32_t baud);

//配置定时器3
extern void tim3_init(void);

//发送蓝牙相关指令给到串口3
extern void blue_send_str(char *str);

#endif
