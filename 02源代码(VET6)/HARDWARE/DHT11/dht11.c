#include "includes.h"

static GPIO_InitTypeDef GPIO_InitStructure;

//配置温湿度模块
void dht11_init(void)
{
	//设置端口E的时钟使能
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE,ENABLE);
	
	//配置端口E的9号引脚，配置为开漏输出模式
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_9;//9号引脚
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_OUT;//输出模式
	GPIO_InitStructure.GPIO_OType=GPIO_OType_OD;//推挽 Push Pull；开漏 Open Drain
	GPIO_InitStructure.GPIO_PuPd=GPIO_PuPd_NOPULL;//不使能上下拉电阻
	GPIO_InitStructure.GPIO_Speed=GPIO_Low_Speed;//低速，功耗低，但是引脚响应时间更长
	GPIO_Init(GPIOE,&GPIO_InitStructure);
	
	PEout(9)=1;
}


//获取温湿度信息(通过查看时序图实现)
int32_t dht11_get_msg(uint8_t *buf)
{
	uint32_t t=0;
	int32_t i,j;
	uint8_t d;
	uint8_t *p=buf;
	uint8_t check_sum=0;
	
	//配置端口E的9号引脚，配置为开漏输出模式
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_9;//9号引脚
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_OUT;//输出模式
	GPIO_InitStructure.GPIO_OType=GPIO_OType_OD;//推挽 Push Pull；开漏 Open Drain
	GPIO_InitStructure.GPIO_PuPd=GPIO_PuPd_NOPULL;//不使能上下拉电阻
	GPIO_InitStructure.GPIO_Speed=GPIO_Low_Speed;//低速，功耗低，但是引脚响应时间更长
	GPIO_Init(GPIOE,&GPIO_InitStructure);	
	
	PEout(9)=0;
	delay_ms(20);  //主机拉低20ms
	PEout(9)=1;
	delay_us(30);  //拉高30us
	
	//配置端口G的9号引脚，配置为输入模式
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_9;//9号引脚
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IN;//输入模式
	//GPIO_InitStructure.GPIO_OType=GPIO_OType_OD;//推挽 Push Pull；开漏 Open Drain
	GPIO_InitStructure.GPIO_PuPd=GPIO_PuPd_NOPULL;//不使能上下拉电阻
	GPIO_InitStructure.GPIO_Speed=GPIO_Low_Speed;//低速，功耗低，但是引脚响应时间更长
	GPIO_Init(GPIOE,&GPIO_InitStructure);
	
	//等待低电平-响应信号出现
	while(PEin(9))
	{
		t++;
		delay_us(1);
		
		//超时就返回错误码
		if(t>=4000)
			return -1;
	}
	t=0;
	//测量低电平-响应信号是否合法
	while(PEin(9) == 0)
	{
		t++;
		delay_us(1);
		
		//该低电平一般维持80us合法
		//超时就返回错误码
		if(t>=100)
			return -2;
	}
	
	t=0;
	//测量高电平-响应信号是否合法
	while(PEin(9))
	{
		t++;
		delay_us(1);
		
		//该低电平一般维持80us合法
		//超时就返回错误码
		if(t>=100)
			return -3;
	}
	
	for(j=0; j<5; j++)
	{
		d=0;  //置回0重新赋值
		//循环接收5个字节，每个字节都是以最高有效位优先接收，最后会完成40bit数据的接收
		for(i=7; i>=0; i--)
		{
			//等待数据0或数据1的前置电平结束
			t=0;
			while(PEin(9)==0)
			{ 
				t++;
				delay_us(1);
				//该低电平一般维持80us合法
				if(t>=100)
					return -4;
			}		
			
			//延时一会(30us~69us)，用于区分数据0还是数据1
			delay_us(40);
				
			//如果是数据1就把该位赋值给d
			if(PEin(9))
			{
				d|=1<<i;//将d变量对应的bit置1
					
				//等待剩下的高电平持续完毕
				t=0;
				while(PEin(9))
				{
					t++;
					delay_us(1);
					if(t>=100)
						return -5;
				}		
			}
		}

		p[j]=d;
	}

	//最后dht11会输出50us的低电平，延时50us略过
	delay_us(50);
	
	//接收完毕后，进行数据的校验
	check_sum=(p[0]+p[1]+p[2]+p[3])&0xFF;
	
	if(check_sum == p[4])
		return 0;
	
	return -6;
}
