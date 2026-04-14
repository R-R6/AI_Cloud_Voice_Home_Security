#ifndef __RTC_H__
#define __RTC_H__

extern RTC_TimeTypeDef RTC_TimeStructure;   		//时间结构体属性 
extern RTC_DateTypeDef RTC_DateStructure;  		//日期结构体属性

//初始化实时时钟
extern void rtc_init(void);

#endif
