#ifndef _SPI_FLASH_H_
#define _SPI_FLASH_H_
#include "stm32f4xx.h"
#include "sys.h"

static GPIO_InitTypeDef  GPIO_InitStructure;  //GPIO端口结构体配置属性

#define W25Q128_CS		PBout(14)
#define W25Q128_SCLK	PBout(3)
#define W25Q128_MOSI	PBout(5)
#define W25Q128_MISO	PBin(4)

extern void delay_ms(uint32_t t);

extern void delay_us(uint32_t t);

//w25q128初始化(SPI FLASH)
void w25q128_init(void);

//模拟api
//发送字节给从机，返回采集的数据
uint8_t SPI1_SendByte(uint8_t byte);

//读取数据
void w25q128_read_data(uint32_t addr,uint8_t *buf,uint32_t len);

//页编程
void w25q128_page_program(uint32_t addr,uint8_t *buf,uint32_t len);

//sflash的写使能(相当于flash的解锁)
void w25q128_write_enable(void);
//sflash的写失能(相当于flash的上锁)
void w25q128_write_disable(void);

//读取statu1
uint8_t w25q128_read_status1(void);

//扇区擦除
void w25q128_sector_erase(uint32_t sector_addr);

#endif
