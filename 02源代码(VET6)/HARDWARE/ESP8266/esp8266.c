#include "includes.h"

static USART_InitTypeDef USART_InitStructure;
static GPIO_InitTypeDef  GPIO_InitStructure;
static NVIC_InitTypeDef  NVIC_InitStructure;

uint8_t  g_esp8266_tx_buf[512];
volatile uint8_t  g_esp8266_rx_buf[512];
volatile uint32_t g_esp8266_rx_cnt=0;

volatile uint32_t g_esp8266_transparent_transmission_sta=0;


//初始化IO 串口6 
//bound:波特率
void usart6_init(uint32_t baud)
{
	//使能端口C硬件时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC,ENABLE);
	
	//使能串口6硬件时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART6,ENABLE);
	
	//配置PC6、PC7为复用功能引脚
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6|GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;	
	GPIO_Init(GPIOC,&GPIO_InitStructure);
	
	//将PC6、PC7连接到USART6的硬件
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource6, GPIO_AF_USART6);
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource7, GPIO_AF_USART6);
	
	
	//配置USART1的相关参数：波特率、数据位、校验位
	USART_InitStructure.USART_BaudRate = baud;//波特率
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//8位数据位
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//1位停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;//允许串口发送和接收数据
	USART_Init(USART6, &USART_InitStructure);
	
	
	//使能串口接收到数据触发中断
	USART_ITConfig(USART6, USART_IT_RXNE, ENABLE);
	
	NVIC_InitStructure.NVIC_IRQChannel = USART6_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
	
	//使能串口6工作
	USART_Cmd(USART6,ENABLE);
}


//发送字符串给到串口6
void usart6_send_str(char *str)
{
	char *p = str;
	
	while(*p!='\0')
	{
		USART_SendData(USART6,*p);
		
		p++;
		
		//等待数据发送成功
		while(USART_GetFlagStatus(USART6,USART_FLAG_TXE)==RESET);
	}
}


//发送字节给到串口6
void usart6_send_bytes(uint8_t *buf,uint32_t len)
{
	uint8_t *p = buf;
	
	while(len--)
	{
		USART_SendData(USART6,*p);
		
		p++;
		
		//等待数据发送成功
		while(USART_GetFlagStatus(USART6,USART_FLAG_TXE)==RESET);
	}
}


//esp8266初始化
void esp8266_init(void)
{
	usart6_init(115200);
}


void esp8266_send_at(char *str)
{
	//清空接收缓冲区
	memset((void *)g_esp8266_rx_buf,0, sizeof g_esp8266_rx_buf);
	
	//清空接收计数值
	g_esp8266_rx_cnt = 0;	
	
	//串口6发送数据
	usart6_send_str(str);
}


void esp8266_send_bytes(uint8_t *buf,uint32_t len)
{
	usart6_send_bytes(buf,len);
}


void esp8266_send_str(char *buf)
{
	usart6_send_str(buf);
}


/* 查找接收数据包中的字符串 */
int32_t esp8266_find_str_in_rx_packet(char *str,uint32_t timeout)
{
	char *dest = str;
	char *src  = (char *)&g_esp8266_rx_buf;
	//等待串口接收完毕或超时退出
	while((strstr(src,dest)==NULL) && timeout)
	{		
		delay_ms(1);
		timeout--;
	}
#if EN_DEBUG_ESP8266	
	printf("[find str]%s ,timeout=%d\r\n",(const char *)g_esp8266_rx_buf,timeout);
#endif
	if(timeout) 
		return 0; 
	                    
	return -1; 
}


/* 自检程序 */
int32_t  esp8266_self_test(void)
{
	esp8266_send_at("AT\r\n");
	
	return esp8266_find_str_in_rx_packet("OK",1000);
}

/**
 * 功能：连接热点
 * 参数：
 *         ssid:热点名
 *         pwd:热点密码
 * 返回值：
 *         连接结果,非0连接成功,0连接失败
 * 说明： 
 *         失败的原因有以下几种(UART通信和ESP8266正常情况下)
 *         1. WIFI名和密码不正确
 *         2. 路由器连接设备太多,未能给ESP8266分配IP
 */
int32_t esp8266_connect_ap(char* ssid,char* pswd)
{
#if 0
	//不建议使用以下sprintf，占用过多的栈
	char buf[128]={0};
	
	sprintf(buf,"AT+CWJAP_CUR=\"%s\",\"%s\"\r\n",ssid,pswd);

#endif
    //设置为STATION模式	
	esp8266_send_at("AT+CWMODE_CUR=1\r\n"); 
	
	if(esp8266_find_str_in_rx_packet("OK",1000))
		return -1;


	//连接目标AP
	//sprintf(buf,"AT+CWJAP_CUR=\"%s\",\"%s\"\r\n",ssid,pswd);
	esp8266_send_at("AT+CWJAP_CUR="); 
	esp8266_send_at("\"");esp8266_send_at(ssid);esp8266_send_at("\"");	
	esp8266_send_at(",");	
	esp8266_send_at("\"");esp8266_send_at(pswd);esp8266_send_at("\"");	
	esp8266_send_at("\r\n");
	if(esp8266_find_str_in_rx_packet("OK",5000))
		if(esp8266_find_str_in_rx_packet("CONNECT",5000))
			return -2;

	return 0;
}



/* 退出透传模式 */
int32_t esp8266_exit_transparent_transmission (void)
{
	//退出透传模式，发送下一条AT指令要间隔1秒
	delay_ms ( 1000 ); 

	esp8266_send_at ("+++");
	
	//退出透传模式，发送下一条AT指令要间隔1秒
	delay_ms ( 1000 ); 

	return 0;
}

/* 进入透传模式 */
int32_t  esp8266_entry_transparent_transmission(void)
{
	//进入透传模式
	esp8266_send_at("AT+CIPMODE=1\r\n");  
	if(esp8266_find_str_in_rx_packet("OK",5000))
		return -1;
	
	delay_ms(2000);
	//开启发送状态
	esp8266_send_at("AT+CIPSEND\r\n");
	if(esp8266_find_str_in_rx_packet(">",5000))
		return -2;

	return 0;
}


/**
 * 功能：使用指定协议(TCP/UDP)连接到服务器
 * 参数：
 *         mode:协议类型 "TCP","UDP"
 *         ip:目标服务器IP
 *         port:目标是服务器端口号
 * 返回值：
 *         连接结果,非0连接成功,0连接失败
 * 说明： 
 *         失败的原因有以下几种(UART通信和ESP8266正常情况下)
 *         1. 远程服务器IP和端口号有误
 *         2. 未连接AP
 *         3. 服务器端禁止添加(一般不会发生)
 */
int32_t esp8266_connect_server(char* mode,char* ip,uint16_t port)
{

#if 0	
	//使用MQTT传递的ip地址过长，不建议使用以下方法，否则导致栈溢出
	//AT+CIPSTART="TCP","a10tC4OAAPc.iot-as-mqtt.cn-shanghai.aliyuncs.com",1883，该字符串占用内存过多了
	
	char buf[128]={0};
	
	//连接服务器
	sprintf((char*)buf,"AT+CIPSTART=\"%s\",\"%s\",%d\r\n",mode,ip,port);
	
	esp8266_send_at(buf);
#else
	
	char buf[16]={0};
	esp8266_send_at("AT+CIPSTART=");
	esp8266_send_at("\"");	esp8266_send_at(mode);	esp8266_send_at("\"");
	esp8266_send_at(",");
	esp8266_send_at("\"");	esp8266_send_at(ip);	esp8266_send_at("\"");	
	esp8266_send_at(",");
	sprintf(buf,"%d",port);
	esp8266_send_at(buf);	
	esp8266_send_at("\r\n");
	
#endif
	
	if(esp8266_find_str_in_rx_packet("CONNECT",5000))
		if(esp8266_find_str_in_rx_packet("OK",5000))
			return -1;
	return 0;
}

/* 断开服务器 */
int32_t esp8266_disconnect_server(void)
{
	esp8266_send_at("AT+CIPCLOSE\r\n");
		
	if(esp8266_find_str_in_rx_packet("CLOSED",5000))
		if(esp8266_find_str_in_rx_packet("OK",5000))
			return -1;
	
	return 0;	
}


/* 使能多链接 */
int32_t esp8266_enable_multiple_id(uint32_t b)
{

	char buf[32]={0};
	
	sprintf(buf,"AT+CIPMUX=%d\r\n", b);
	esp8266_send_at(buf);
	
	if(esp8266_find_str_in_rx_packet("OK",5000))
		return -1;
	
	return 0;
}

/* 创建服务器 */
int32_t esp8266_create_server(uint16_t port)
{
	char buf[32]={0};
	
	sprintf(buf,"AT+CIPSERVER=1,%d\r\n", port);
	esp8266_send_at(buf);
	
	if(esp8266_find_str_in_rx_packet("OK",5000))
		return -1;
	
	return 0;
}

/* 关闭服务器 */
int32_t esp8266_close_server(uint16_t port)
{
	char buf[32]={0};
	
	sprintf(buf,"AT+CIPSERVER=0,%d\r\n", port);
	esp8266_send_at(buf);
	
	if(esp8266_find_str_in_rx_packet("OK",5000))
		return -1;
	
	return 0;
}

/* 回显打开或关闭 */
int32_t esp8266_enable_echo(uint32_t b)
{
	if(b)
		esp8266_send_at("ATE1\r\n"); 
	else
		esp8266_send_at("ATE0\r\n"); 
	
	if(esp8266_find_str_in_rx_packet("OK",5000))
		return -1;

	return 0;
}

/* 复位 */
int32_t esp8266_reset(void)
{
	esp8266_send_at("AT+RST\r\n");
	
	if(esp8266_find_str_in_rx_packet("OK",10000))
		return -1;

	return 0;
}


//串口6的中断服务函数
void USART6_IRQHandler(void)
{
	uint8_t d=0;
	
	uint32_t ulReturn;
	
	
	/* 进入临界段，临界段可以嵌套 */
	ulReturn = taskENTER_CRITICAL_FROM_ISR();
	
	//检测是否接收到数据
	if (USART_GetITStatus(USART6, USART_IT_RXNE) == SET)
	{
		d=USART_ReceiveData(USART6);
		
		if(g_esp8266_rx_cnt<(sizeof(g_esp8266_rx_buf)-1))
			g_esp8266_rx_buf[g_esp8266_rx_cnt++]=d;
		
	
		//清空标志位，可以响应新的中断请求
		USART_ClearITPendingBit(USART6, USART_IT_RXNE);
	}
	
	/* 退出临界段 */
	taskEXIT_CRITICAL_FROM_ISR( ulReturn );
}
