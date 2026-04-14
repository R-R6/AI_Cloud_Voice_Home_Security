#ifndef _EEPROM_H_
#define _EEPROM_H_
#include "stm32f4xx.h"
#include "sys.h"

extern void at24c02_init(void);	//eeprom初始化

extern int32_t at24c02_write(uint8_t word_addr,uint8_t *buf,uint32_t len);		//写入数据

extern int32_t  at24c02_read(uint8_t word_addr,uint8_t *buf,uint32_t len);		//读取数据
#endif
