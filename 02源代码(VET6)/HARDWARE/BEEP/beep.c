#include "includes.h"

static GPIO_InitTypeDef  GPIO_InitStructure;
//static TIM_OCInitTypeDef  TIM_OCInitStructure;
//static TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;

//volatile uint32_t tim13_cnt=0;

////定时器3初始化
//void tim13_init(void)
//{	
//	/* 定时器13的时钟使能*/
//	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM13, ENABLE);
//	
//	/*定时器的基本配置，用于配置定时器的输出脉冲的频率为400Hz  84000000/2100 */
//	TIM_TimeBaseStructure.TIM_Period = (40000/100)-1;					//设置定时脉冲的频率
//	TIM_TimeBaseStructure.TIM_Prescaler = 2100-1;						//第一次分频，简称为预分频
//	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;				//第二次分频,当前实现1分频，也就是不分频
//	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
//	
//	tim13_cnt=TIM_TimeBaseStructure.TIM_Period;
//	
//	TIM_TimeBaseInit(TIM13, &TIM_TimeBaseStructure);

//	/* 配置PF8 引脚为复用模式 */
//	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;					//第8根引脚
//	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;				//设置复用模式
//	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;				//推挽模式，增加驱动电流
//	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;			//设置IO的速度为100MHz，频率越高性能越好，频率越低，功耗越低
//	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;			//不需要上拉电阻
//	GPIO_Init(GPIOF, &GPIO_InitStructure);	
//	
//	GPIO_PinAFConfig(GPIOF, GPIO_PinSource8, GPIO_AF_TIM13);
//	
//	
//	/* 让定时器14 PWM 的通道 1 工作在模式 1*/
//	 //PWM 模式 1， 在递增模式下， 只要TIMx_CNT < TIMx_CCR1， 通道 1 便为有效状态（高电平）， 否则为无效状态（低电平）。
//	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
//	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable; //允许输出
//	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High; 		//有效的时候， 输出高电平
//	TIM_OC1Init(TIM13, &TIM_OCInitStructure);
//	
//	/*  使能定时器 13 工作 */
//	TIM_Cmd(TIM13, ENABLE);
//}

////设置定时器13的PWM频率
//void tim13_set_freq(uint32_t freq)
//{
//	/*定时器的基本配置，用于配置定时器的输出脉冲的频率为freq Hz */
//	TIM_TimeBaseStructure.TIM_Period = (40000/freq)-1;					//设置定时脉冲的频率
//	TIM_TimeBaseStructure.TIM_Prescaler = 2100-1;						//第一次分频，简称为预分频
//	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;				//第二次分频,当前实现1分频，也就是不分频
//	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;

//	tim13_cnt= TIM_TimeBaseStructure.TIM_Period;
//	
//	TIM_TimeBaseInit(TIM13, &TIM_TimeBaseStructure);
//}

////设置定时器13的PWM占空比0%~100%
//void tim13_set_duty(uint32_t duty)
//{
//	uint32_t cmp=0;
//	
//	cmp = (tim13_cnt+1) * duty/100;  //tim13_cnt决定声音的频率(快慢)，duty决定声音的大小

//	TIM_SetCompare1(TIM13,cmp);
//}


//蜂鸣器初始化
void beep_init(void)
{
//	tim13_init();
//	
//	//蜂鸣器禁鸣
//	tim13_set_duty(0);
	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);	//使能GPIOE时钟
	
	/* 配置PE8 引脚为输出模式 */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;					//第1根引脚
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;				//设置输出模式
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;				//推挽模式，增加驱动电流
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;			//设置IO的速度为100MHz，频率越高性能越好，频率越低，功耗越低
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;			//不需要上拉电阻
	GPIO_Init(GPIOE, &GPIO_InitStructure);	
	BEEP=0;
}
