#ifndef __INCLUDES_H__
#define __INCLUDES_H__

/* 标准C库*/
#include <stdio.h>	
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"
#include "queue.h"

/* 外设相关 */
#include "sys.h"  			//系统参数
#include "delay.h"  		//延时
#include "usart.h"  		//串口
#include "led.h"  			//led灯
#include "sr04.h"  		//sr04超声波
#include "key.h" 			//按键
#include "beep.h"  		//蜂鸣器
#include "iwdg.h"   		//独立看门狗
#include "oled.h"   		//oled显示屏
#include "oledfont.h"   	//oled字体库	
#include "bmp.h"			//oled图像库
#include "MFRC522.h" 		//rfid读卡器
#include "eeprom.h"		//eeprom闪存
#include "rtc.h"			//RTC
#include "lm393.h"			//火焰传感器
#include "mq2.h"			//气体传感器
#include "dht11.h"			//温湿度传感器
#include "bluetooth.h"		//蓝牙
#include "aspro.h"			//语音设别模块
#include "esp8266.h"		//无线WiFi模块-esp8266
#include "esp8266_mqtt.h"	//esp8266的mqtt协议封装

/* 宏定义 */
#define EVENT_GROUP_RTC_WAKEUP		0x01
#define EVENT_GROUP_KEY1_DOWN		0x02
#define EVENT_GROUP_KEY2_DOWN		0x04
#define EVENT_GROUP_KEY3_DOWN		0x08
#define EVENT_GROUP_KEY4_DOWN		0x10


/* 变量 */
extern SemaphoreHandle_t 	g_mutex_printf;
extern SemaphoreHandle_t 	g_mutex_card;
extern SemaphoreHandle_t    g_mutex_alarm;
/* ESP8266 USART3 AT 收发互斥：避免 mqtt 上报与 monitor 入队抢 g_esp8266_rx_buf */
extern SemaphoreHandle_t 	g_mutex_esp8266;

extern EventGroupHandle_t 	g_event_group;

// 传感器状态全局变量（0=安全，1=报警）
extern volatile uint8_t g_fire_status;
extern volatile uint8_t g_mq2_status;

// 温湿度全局变量
extern float g_temp;
extern float g_humi;

/* 函数声明 */
extern void dgb_printf_safe(const char *format, ...);  // 线程安全的调试打印函数

/* 类型 */
typedef struct __oled_t
{

#define OLED_CTRL_DISPLAY_ON        0x01	//亮屏标志位
#define OLED_CTRL_DISPLAY_OFF       0x02	//灭屏标志位
#define OLED_CTRL_INIT              0x03	//初始化标志位
#define OLED_CTRL_CLEAR             0x04	//清屏标志位
#define OLED_CTRL_SHOW_STRING       0x05	//显示字符串标志位
#define OLED_CTRL_SHOW_CHINESE      0x06	//显示汉字标志位
#define OLED_CTRL_SHOW_PICTURE      0x07	//显示图片标志位

	uint8_t ctrl;		//oled操作命令
	uint8_t x;			//x坐标
	uint8_t y;			//y坐标

	uint8_t *str;		//字符串
	uint8_t font_size;	//字符串大小
    uint8_t chinese;	//中文字所在下表
	
	const uint8_t *pic;	//图片
	uint8_t pic_width;	//图片宽度
	uint8_t pic_height;	//图片长度
}oled_t;

typedef struct __eeprom_t
{
#define AT24C02_READ		0x01	//读取eeprom闪存标志位
#define AT24C02_WRITE		0x02	//写入eeprom闪存标志位

	uint8_t ctrl;			//eeprom操作命令
	uint8_t	*data;			//数据
	uint8_t addr;			//eeprom地址
	uint32_t size;			//数据大小
	
}eeprom_t;

typedef struct __task_t
{
    TaskFunction_t pxTaskCode;              // 任务函数指针
    const char * const pcName;              // 任务名称(字符串常量)
    const configSTACK_DEPTH_TYPE usStackDepth;  // 任务栈大小
    void * const pvParameters;              // 任务参数指针
    UBaseType_t uxPriority;                 // 任务优先级
    TaskHandle_t * const pxCreatedTask;     // 任务句柄指针(用于后续控制)
} task_t;

#endif
