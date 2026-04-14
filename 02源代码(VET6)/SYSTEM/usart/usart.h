#ifndef __USART_H
#define __USART_H

#include "stdio.h"	
#include "stm32f4xx_conf.h"
#include "sys.h" 

//如果想串口中断接收，请不要注释以下宏定义
extern void uart1_init(u32 baud);
extern void usart2_init(u32 baud);
extern void usart3_init(u32 baud);

extern volatile uint8_t  g_usart2_rx_buf[128];	//接收到的数据
extern volatile uint32_t g_usart2_rx_cnt; 		//接收到的数量
extern volatile uint32_t g_usart2_rx_end;  		//接收是否完毕的标志位

extern volatile uint8_t  g_usart3_rx_buf[128];	//接收到的数据
extern volatile uint32_t g_usart3_rx_cnt; 		//接收到的数量
extern volatile uint32_t g_usart3_rx_end;  		//接收是否完毕的标志位


extern void usart_send_str(USART_TypeDef* USARTx,char *str);
extern void usart_send_bytes(USART_TypeDef* USARTx,uint8_t *buf,uint32_t len);

#endif

