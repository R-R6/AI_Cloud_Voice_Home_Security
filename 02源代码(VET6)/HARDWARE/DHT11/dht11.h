#ifndef __DHT11_H
#define __DHT11_H

/*	
	温湿度模块引脚：PG9
*/
//配置温湿度模块
extern void dht11_init(void);

//获取温湿度信息(通过查看时序图实现)
extern int32_t dht11_get_msg(uint8_t *buf);

#endif

