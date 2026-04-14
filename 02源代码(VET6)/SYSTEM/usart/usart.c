#include "sys.h"
#include "usart.h"	
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"


volatile uint8_t  g_usart1_rx_buf[512];
volatile uint32_t g_usart1_rx_cnt=0;
volatile uint32_t g_usart1_rx_end=0;

volatile uint8_t  g_usart2_rx_buf[128]={0};	//接收到的数据
volatile uint32_t g_usart2_rx_cnt=0; 		//接收到的数量
volatile uint32_t g_usart2_rx_end=0;  		//接收是否完毕的标志位

volatile uint8_t  g_usart3_rx_buf[128]={0};	//接收到的数据
volatile uint32_t g_usart3_rx_cnt=0; 		//接收到的数量
volatile uint32_t g_usart3_rx_end=0;  		//接收是否完毕的标志位

static GPIO_InitTypeDef  GPIO_InitStructure; //GPIO端口结构体配置属性
static NVIC_InitTypeDef NVIC_InitStructure;  //优先级结构体配置属性
static USART_InitTypeDef USART_InitStructure;//串口结构体初始化属性


//加入以下代码,支持printf函数,而不需要选择use MicroLIB	  
#pragma import(__use_no_semihosting)             
//标准库需要的支持函数                 
struct __FILE 
{ 
	int handle; 
}; 

FILE __stdout;   

//定义_sys_exit()以避免使用半主机模式    
void _sys_exit(int x) 
{ 
	x = x; 
} 

//重定义fputc函数 （printf 映射到串口1）
int fputc(int ch, FILE *f)
{ 	
	while((USART1->SR&0X40)==0);//循环发送,直到发送完毕   
	USART1->DR = (u8) ch;      
	return ch;
}

// 串口发送字符函数
void usart_send_str(USART_TypeDef* USARTx,char *str)
{
	char *p = str;
	
	while(*p!='\0')
	{
		USART_SendData(USARTx,*p);
		
		p++;
	
		//等待数据发送成功
		while(USART_GetFlagStatus(USARTx,USART_FLAG_TXE)==RESET);
		USART_ClearFlag(USARTx,USART_FLAG_TXE);
	}
}

// 串口发送多字节数据
void usart_send_bytes(USART_TypeDef* USARTx,uint8_t *buf,uint32_t len)
{
	uint8_t *p = buf;
	
	while(len--)
	{
		USART_SendData(USARTx,*p);
		
		p++;
		
		//等待数据发送成功
		while(USART_GetFlagStatus(USARTx,USART_FLAG_TXE)==RESET);
		USART_ClearFlag(USARTx,USART_FLAG_TXE);
	}
}

//初始化IO 串口1 （打印日志）
//bound:波特率
void uart1_init(u32 baud)
{
	//GPIO端口设置
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA,ENABLE); //使能GPIOA时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE);//使能USART1时钟
 
	//串口1对应引脚复用映射
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource9,GPIO_AF_USART1); //GPIOA9复用为USART1
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource10,GPIO_AF_USART1); //GPIOA10复用为USART1
	
	//USART1端口配置
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10; //GPIOA9与GPIOA10
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;//复用功能
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;	//速度50MHz
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP; //推挽复用输出
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; //上拉
	GPIO_Init(GPIOA,&GPIO_InitStructure); //初始化PA9，PA10

   //USART1 初始化设置
	USART_InitStructure.USART_BaudRate = baud;//波特率设置
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式
	USART_Init(USART1, &USART_InitStructure); //初始化串口1
	
	USART_Cmd(USART1, ENABLE);  //使能串口1 
	
	//USART_ClearFlag(USART1, USART_FLAG_TC);
	

	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//开启相关中断

	//Usart1 NVIC 配置
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;//串口1中断通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;//抢占优先级configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
	NVIC_InitStructure.NVIC_IRQChannelSubPriority =3;		//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器、
}


//初始化IO 串口2 （语音模块）
//bound:波特率
void usart2_init(u32 baud)
{
	//GPIO端口设置	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA,ENABLE); //使能GPIOA时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2,ENABLE);//使能USART2时钟
 
	//串口2对应引脚复用映射   PA2 PA3
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource2,GPIO_AF_USART2); //GPIOA2复用为USART2
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource3,GPIO_AF_USART2); //GPIOA3复用为USART2
	
	//USART2端口配置
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3; //GPIOA2与GPIOA3
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;//复用功能
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;	//速度50MHz
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP; //推挽复用输出
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; //上拉
	GPIO_Init(GPIOA,&GPIO_InitStructure); //初始化PA2，PA3

   //USART2 初始化设置
	USART_InitStructure.USART_BaudRate = baud;//波特率设置
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式
	USART_Init(USART2, &USART_InitStructure); //初始化串口2
	
	USART_Cmd(USART2, ENABLE);  //使能串口2
	
	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);//开启相关中断

	//USART2 NVIC 配置
	NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;//串口2中断通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;//抢占优先级configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
	NVIC_InitStructure.NVIC_IRQChannelSubPriority =3;		//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器、
}

/*
	bref：配置串口3 （蓝牙模块）PC10 PC11
	param1：uint32_t baud(波特率)
*/
void usart3_init(uint32_t baud)
{
	//硬件时钟，端口，串口
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC,ENABLE);
	
	//串口引脚要配置为复用功能模式
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_10|GPIO_Pin_11;//10 11号引脚
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_AF;//复用功能模式（引脚既可以软件代码控制，也可以其他外设控制）
	GPIO_InitStructure.GPIO_OType=GPIO_OType_PP;//推挽 Push Pull；开漏 Open Drain
	GPIO_InitStructure.GPIO_PuPd=GPIO_PuPd_NOPULL;//不使能上下拉电阻
	GPIO_InitStructure.GPIO_Speed=GPIO_High_Speed;//高速，功耗高，但是引脚响应时间更短
	GPIO_Init(GPIOC,&GPIO_InitStructure);	
	
	//指定引脚的功能，连接到串口3
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource10, GPIO_AF_USART3);
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource11, GPIO_AF_USART3);	
	
	//配置串口3相关的参数
	USART_InitStructure.USART_BaudRate = baud;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART3, &USART_InitStructure);
	
	//使能串口3工作
	USART_Cmd(USART3, ENABLE);
	
	//若需要中断，则配置中断相关参数
	USART_ITConfig(USART3,USART_IT_RXNE,ENABLE);
	
	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}


//串口1中断服务程序
void USART1_IRQHandler(void)                	
{
	uint8_t d;
	
	if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)  
	{
		//接收串口数据
		d=USART_ReceiveData(USART1);

		printf("%c",d);
		
		//清空串口接收中断标志位
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	} 
} 


void USART2_IRQHandler(void)
{
	uint8_t d;

	uint32_t ulReturn;
	
	/* 进入临界段，临界段可以嵌套 */
	ulReturn = taskENTER_CRITICAL_FROM_ISR();	
	
	if(USART_GetITStatus(USART2,USART_IT_RXNE) == SET)
	{
		d = USART_ReceiveData(USART2);
		
		g_usart2_rx_buf[g_usart2_rx_cnt++]=d;

		
		if(d == '#' || g_usart2_rx_cnt>=sizeof(g_usart2_rx_buf))
		{
			g_usart2_rx_end=1;
		}
		
		/* 告诉CPU，已经完成接收中断请求，可以响应新的接收中断请求 */
		USART_ClearITPendingBit(USART2,USART_IT_RXNE);
		
	}
	
	/* 退出临界段 */
	taskEXIT_CRITICAL_FROM_ISR( ulReturn );
}


//串口3中断服务函数
void USART3_IRQHandler(void)
{
	uint8_t d;
	
	//检测标志位
	if(USART_GetITStatus(USART3,USART_IT_RXNE) == SET)
	{
		//添加用户代码
		d= USART_ReceiveData(USART3);
		
		//一个字节一个字节的存入数组中
		if(g_usart3_rx_cnt<sizeof(g_usart3_rx_buf))
			g_usart3_rx_buf[g_usart3_rx_cnt++]=d;
	
		//清空标志位	
		USART_ClearITPendingBit(USART3,USART_IT_RXNE);
	}
}






