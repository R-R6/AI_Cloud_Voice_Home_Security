#include "includes.h"

static RTC_InitTypeDef RTC_InitStructure;  	//实时时钟结构体初始化属性
RTC_TimeTypeDef RTC_TimeStructure;   		//时间结构体属性 
RTC_DateTypeDef RTC_DateStructure;  		//日期结构体属性


static EXTI_InitTypeDef EXTI_InitStructure;  //中断结构体配置属性
static NVIC_InitTypeDef NVIC_InitStructure;  //优先级结构体配置属性

//初始化实时时钟
void rtc_init(void)
{
	/* Enable the PWR clock ,使能电源时钟*/
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);

	/* Allow access to RTC ，允许访问RTC*/
	PWR_BackupAccessCmd(ENABLE);
	
	/* 使能LSI*/
	RCC_LSICmd(ENABLE);
	
	/* 检查该LSI是否有效*/  
	while(RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET);
	
	/* 选择LSI作为RTC的硬件时钟源*/
	RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);

	/* ck_spre(1Hz) = RTCCLK(LSE) /(uwAsynchPrediv + 1)/(uwSynchPrediv + 1)*/
	/* Enable the RTC Clock ，使能RTC时钟*/
	RCC_RTCCLKCmd(ENABLE);
	
	/* Wait for RTC APB registers synchronisation ，等待RTC相关寄存器就绪*/
	RTC_WaitForSynchro();
	
	/* Configure the RTC data register and RTC prescaler，配置RTC数据寄存器与RTC的分频值 */
	//LSI时钟频率为32kHz
	RTC_InitStructure.RTC_AsynchPrediv = 0x7F;				//异步分频系数  128
	RTC_InitStructure.RTC_SynchPrediv = 0xF9;				//同步分频系数  250
	RTC_InitStructure.RTC_HourFormat = RTC_HourFormat_24;	//24小时格式
	RTC_Init(&RTC_InitStructure);

	/* 占位初值：联网后由 esp8266_nettime_sync() 写入北京时间，勿再写死演示日期 */
	RTC_DateStructure.RTC_Year    = 0x00;
	RTC_DateStructure.RTC_Month   = 0x01;
	RTC_DateStructure.RTC_Date    = 0x01;
	RTC_DateStructure.RTC_WeekDay = 0x06;
	RTC_SetDate(RTC_Format_BCD, &RTC_DateStructure);

	RTC_TimeStructure.RTC_H12     = RTC_H12_AM;
	RTC_TimeStructure.RTC_Hours   = 0x00;
	RTC_TimeStructure.RTC_Minutes = 0x00;
	RTC_TimeStructure.RTC_Seconds = 0x00;
	RTC_SetTime(RTC_Format_BCD, &RTC_TimeStructure);
	
	//关闭唤醒功能
	RTC_WakeUpCmd(DISABLE);
	
	//为唤醒功能选择RTC配置好的时钟源
	RTC_WakeUpClockConfig(RTC_WakeUpClock_CK_SPRE_16bits);
	
	//设置唤醒计数值为自动重载，写入值默认是0
	RTC_SetWakeUpCounter(1-1);
	
	//清除RTC唤醒中断标志
	RTC_ClearITPendingBit(RTC_IT_WUT);
	
	//使能RTC唤醒定时器中断,就是计数1次完成后，就触发中断
	RTC_ITConfig(RTC_IT_WUT, ENABLE);
	
	//使能唤醒功能
	RTC_WakeUpCmd(ENABLE);
	
	/* Configure EXTI Line22，配置外部中断控制线22 */
	EXTI_InitStructure.EXTI_Line = EXTI_Line22;			//当前使用外部中断控制线22
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;		//中断模式
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;		//上升沿触发中断 
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;			//使能外部中断控制线22
	EXTI_Init(&EXTI_InitStructure);
	
	NVIC_InitStructure.NVIC_IRQChannel = RTC_WKUP_IRQn;		//允许RTC唤醒中断触发
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;	//抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x03;		//响应优先级为0x3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//使能
	NVIC_Init(&NVIC_InitStructure);	
}

/**
 * @brief 通知 app_task_rtc 立即从 RTC 寄存器刷新 OLED 时间/日期（网络对时后调用）。
 */
void rtc_notify_oled_refresh(void)
{
	if (g_event_group != NULL)
		(void)xEventGroupSetBits(g_event_group, (EventBits_t)EVENT_GROUP_RTC_WAKEUP);
}

//实时时钟的中断服务函数
void RTC_WKUP_IRQHandler(void)
{
	//检测标志位
	if(RTC_GetITStatus(RTC_IT_WUT) != RESET)
	{
		//设置事件标志组
		xEventGroupSetBitsFromISR(g_event_group,EVENT_GROUP_RTC_WAKEUP,NULL);	
	
		//清空标志位	
		RTC_ClearITPendingBit(RTC_IT_WUT);  //清除唤醒定时器中断
		EXTI_ClearITPendingBit(EXTI_Line22);  //清除外部中断线
	}
}


