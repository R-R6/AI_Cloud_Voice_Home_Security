//#include "includes.h"

//static GPIO_InitTypeDef  GPIO_InitStructure;  //GPIO端口结构体配置属性

//static ADC_InitTypeDef       ADC_InitStructure;  //adc结构体特定初始化属性
//static ADC_CommonInitTypeDef ADC_CommonInitStructure;  //adc结构体普通初始化属性

//// PC0-A0: ADC12_IN10   
//// PB7-D0: TTL开关信号输出

////adc1初始化
//void lm393_init(void)
//{
//	//端口C的时钟使能
//	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC,ENABLE);	
//	//端口B的时钟使能
//	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB,ENABLE);
//	//使能ADC1时钟
//	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);	

//	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
//	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;  //模拟信号模式
//	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN ; // 这里改下拉！悬空的话就是高电平（会一直读到4095的无效值）
//	GPIO_Init(GPIOC, &GPIO_InitStructure);
//	
//	//配置LM393的TTL引脚PB7为输出模式
//	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_7;//7号引脚
//	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_OUT;//输出模式（引脚既可以软件代码控制，也可以其他外设控制）
//	GPIO_InitStructure.GPIO_OType=GPIO_OType_PP;//推挽 Push Pull；开漏 Open Drain
//	GPIO_InitStructure.GPIO_PuPd=GPIO_PuPd_NOPULL;//不使能上下拉电阻
//	GPIO_InitStructure.GPIO_Speed=GPIO_High_Speed;//高速，功耗高，但是引脚响应时间更短
//	GPIO_Init(GPIOB,&GPIO_InitStructure);	
//	FIRE_D0=1;	
//	
////	// ===================== 2.ADC 复位 =====================
////  RCC_APB2PeriphResetCmd(RCC_APB2Periph_ADC1, ENABLE);
////  RCC_APB2PeriphResetCmd(RCC_APB2Periph_ADC1, DISABLE);
//	
//	/* ADC Common Init **********************************************************/
//	ADC_CommonInitStructure.ADC_Mode = ADC_Mode_Independent;//单个ADC工作
//	ADC_CommonInitStructure.ADC_Prescaler = ADC_Prescaler_Div2;//ADC硬件时钟频率=84MHz/2=42MHz
//	ADC_CommonInitStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;//关闭DMA模式
//	ADC_CommonInitStructure.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;//两个采样点的间隔时间=5*（1/42MHz）
//	ADC_CommonInit(&ADC_CommonInitStructure);
//	
//	// ===================== 修复3：关闭连续转换 =====================
//	/* ADC1 Init ****************************************************************/
//	ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b;//分辨率12bit，分辨率越高，得到数据更准确，灵敏度更高
//	ADC_InitStructure.ADC_ScanConvMode = DISABLE;//ADC1只转换1个通道
//	ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;//ADC连续不断的进行转换；若是DISABLE就转换一次
//	ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;//不需要边沿触发工作，使用软件触发ADC工作
//	//ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T1_CC1;
//	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;//右对齐存储
//	ADC_InitStructure.ADC_NbrOfConversion = 1;//只转换1个通道，恒为1；如果ADC转换多个通道，请指定数量
//	ADC_Init(ADC1, &ADC_InitStructure);
//	
//	/* ADC1 regular channel4 configuration，顺序为1，采样时间为3*(1/42MHz)*/
//	ADC_RegularChannelConfig(ADC1, ADC_Channel_10, 1, ADC_SampleTime_480Cycles); // 加长采样时间

//	//使能adc1
//	ADC_Cmd(ADC1, ENABLE);
//}


//// 读取ADC值
//uint16_t fire_get_adc(void)
//{
//	ADC_SoftwareStartConv(ADC1);
//	while(ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);
//	return ADC_GetConversionValue(ADC1);
//}

#include "includes.h"

static GPIO_InitTypeDef  GPIO_InitStructure;

// PB7 - D0：火焰传感器数字输出（纯高低电平）
void lm393_init(void)
{
	// 端口B时钟使能
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	
	// 配置 PB7 为 **输入模式** 读取火焰传感器 DO 电平
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;  		// 输入模式
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL; 		// 硬件上拉
	GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
	GPIO_Init(GPIOB, &GPIO_InitStructure);	
}

// 读取火焰传感器 DO 引脚电平
// 返回 0 = 有火   返回 1 = 无火
uint8_t fire_get_d0(void)
{
	return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_7);
}
