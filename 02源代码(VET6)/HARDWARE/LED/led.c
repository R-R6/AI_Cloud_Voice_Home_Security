#include "includes.h" 

static GPIO_InitTypeDef  GPIO_InitStructure;

void led_init(void)
{    	 
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE|RCC_AHB1Periph_GPIOF, ENABLE);	//使能GPIOF、GPIOF时钟

	//GPIOF9,F10初始化设置
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;	//9号和10号引脚
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;			//普通输出模式
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;			//推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;		//100MHz
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;			//上拉
	GPIO_Init(GPIOF, &GPIO_InitStructure);					//初始化GPIO
	GPIO_SetBits(GPIOF,GPIO_Pin_9 | GPIO_Pin_10);			//GPIOF9,F10设置高，灯灭
	
	/* Configure PE13 PE14 in output pushpull mode，配置PE13 PE14引脚为输出模式 */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13|GPIO_Pin_14;		//第13根和14根引脚
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;				//设置输出模式
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;				//推挽模式，增加驱动电流
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;			//设置IO的速度为100MHz，频率越高性能越好，频率越低，功耗越低
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;			//不需要上拉电阻
	GPIO_Init(GPIOE, &GPIO_InitStructure);	
	GPIO_SetBits(GPIOE,GPIO_Pin_13 | GPIO_Pin_14);			//GPIOE13,E14设置高，灯灭
}






