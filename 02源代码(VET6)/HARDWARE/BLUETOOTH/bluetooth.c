#include "includes.h"

static NVIC_InitTypeDef NVIC_InitStructure;  			//优先级结构体配置属性
static TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;  //定时器结构体初始化属性

void blue_init(uint32_t baud)
{
	uart4_init(baud);
}

//配置定时器3
void tim3_init(void)
{
	//使能tim3的硬件时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
	
	//配置tim3的分频值、计数值
	//tim3硬件时钟=84MHz/8400=10000Hz，就是进行10000次计数，就是1秒时间的到达
	TIM_TimeBaseStructure.TIM_Period = 10000/1000-1; //计数值0 -> 9就是10毫秒时间的到达
	TIM_TimeBaseStructure.TIM_Prescaler = 8400-1;	//预分频值8400
	TIM_TimeBaseStructure.TIM_ClockDivision = 0;	//时钟分频，当前是没有的，不需要进行配置
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
	
	//配置tim3的中断
	TIM_ITConfig(TIM3,TIM_IT_Update,ENABLE);
	
	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
	
	//使能定时器3
	TIM_Cmd(TIM3,ENABLE);
}



//发送蓝牙相关指令给到串口4(UART4)
void blue_send_str(char *str)
{
	usart_send_str(UART4, str);
}


//定时器3中断服务函数
void TIM3_IRQHandler(void)
{
	static uint32_t cnt=0;

	//检测标志位
	if(TIM_GetITStatus(TIM3,TIM_IT_Update) == SET)
	{
		if(cnt!=g_usart3_rx_cnt)
		{
			cnt=g_usart3_rx_cnt;
		}
		else if(cnt && (cnt == g_usart3_rx_cnt))  //发送完毕
		{
			g_usart3_rx_end=1;
			cnt=0;
		}
		
		//清空标志位
		TIM_ClearITPendingBit(TIM3,TIM_IT_Update);
	}
}
