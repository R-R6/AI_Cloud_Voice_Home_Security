//#include "includes.h"

//static GPIO_InitTypeDef  GPIO_InitStructure;  //GPIO端口结构体配置属性

//static ADC_InitTypeDef       ADC_InitStructure;  //adc结构体特定初始化属性
//static ADC_CommonInitTypeDef ADC_CommonInitStructure;  //adc结构体普通初始化属性

////A0==>PA6:ADC12_IN6   PA1:ADC12_IN1
////ADC2初始化
//void mq2_init(void)
//{
//	//端口A的时钟使能
//	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA,ENABLE);
//	//使能ADC2时钟
//	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC2, ENABLE);	
//	
//	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
//	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;  //模拟信号模式
//	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL ; // 1.这里改下拉！
//	GPIO_Init(GPIOA, &GPIO_InitStructure);

//	// ===================== 2.ADC 复位 =====================
//  RCC_APB2PeriphResetCmd(RCC_APB2Periph_ADC2, ENABLE);
//  RCC_APB2PeriphResetCmd(RCC_APB2Periph_ADC2, DISABLE);
//	
//	/* ADC Common Init **********************************************************/
//	ADC_CommonInitStructure.ADC_Mode = ADC_Mode_Independent;//单个ADC工作
//	ADC_CommonInitStructure.ADC_Prescaler = ADC_Prescaler_Div2;//ADC硬件时钟频率=84MHz/2=42MHz
//	ADC_CommonInitStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;//关闭DMA模式
//	ADC_CommonInitStructure.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;//两个采样点的间隔时间=5*（1/42MHz）
//	ADC_CommonInit(&ADC_CommonInitStructure);
//	
//	 // ===================== 修复3：关闭连续转换 =====================
//	/* ADC2 Init ****************************************************************/
//	ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b;//分辨率12bit，分辨率越高，得到数据更准确，灵敏度更高
//	ADC_InitStructure.ADC_ScanConvMode = DISABLE;//ADC2只转换1个通道
//	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;//ADC连续不断的进行转换；若是DISABLE就转换一次
//	ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;//不需要边沿触发工作，使用软件触发ADC工作
//	//ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T1_CC1;
//	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;//右对齐存储
//	ADC_InitStructure.ADC_NbrOfConversion = 1;//只转换1个通道，恒为1；如果ADC转换多个通道，请指定数量
//	ADC_Init(ADC2, &ADC_InitStructure);
//	
//	/* ADC2 regular channel 1 configuration，顺序为1，采样时间为3*(1/42MHz)*/
//	ADC_RegularChannelConfig(ADC2, ADC_Channel_1, 1, ADC_SampleTime_3Cycles);  // 加长采样时间

//	//使能ADC2
//	ADC_Cmd(ADC2, ENABLE);
//	
//}

//u16 mq2_get_adc(void)
//{
//    ADC_SoftwareStartConv(ADC2);
//    while(ADC_GetFlagStatus(ADC2, ADC_FLAG_EOC) == RESET);
//    return ADC_GetConversionValue(ADC2);
//}


#include "includes.h"

static GPIO_InitTypeDef  GPIO_InitStructure;  //GPIO端口结构体配置属性

// 注释掉ADC相关结构体（不再使用）
//static ADC_InitTypeDef       ADC_InitStructure;  //adc结构体特定初始化属性
//static ADC_CommonInitTypeDef ADC_CommonInitStructure;  //adc结构体普通初始化属性

// MQ-2 修改为：DO数字电平输出 
// 示例：DO --> PA1 （你硬件接哪个引脚就改哪个）
#define MQ2_DO_GPIO_PORT    GPIOA
#define MQ2_DO_GPIO_PIN     GPIO_Pin_1
#define MQ2_DO_GPIO_CLK     RCC_AHB1Periph_GPIOA

//MQ2传感器初始化（改为DO口输入）
void mq2_init(void)
{
	// 端口时钟使能
	RCC_AHB1PeriphClockCmd(MQ2_DO_GPIO_CLK, ENABLE);
	
	// 配置MQ2的DO引脚为输入模式（模块已有硬件上拉，无需内部上下拉）
	GPIO_InitStructure.GPIO_Pin = MQ2_DO_GPIO_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;  // 输入模式
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL; // 无上下拉（模块自带硬件上拉）
	GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
	GPIO_Init(MQ2_DO_GPIO_PORT, &GPIO_InitStructure);
}

// 读取MQ2 DO引脚电平
// 返回值：0=有烟雾  1=无烟雾
uint8_t mq2_get_do(void)
{
	return GPIO_ReadInputDataBit(MQ2_DO_GPIO_PORT, MQ2_DO_GPIO_PIN);
}
