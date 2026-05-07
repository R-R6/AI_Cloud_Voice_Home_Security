#include "includes.h"

//发送字符串给到串口3(ESP8266使用USART3)
void usart3_send_str(char *str)
{
	usart_send_str(USART3,str);
}


//发送字节给到串口3(ESP8266使用USART3)
void usart3_send_bytes(uint8_t *buf,uint32_t len)
{
	usart_send_bytes(USART3,buf,len);
}


//esp8266初始化
void esp8266_init(uint32_t baud)
{
	// 注意: ESP8266硬件连接到PB10/PB11,这些引脚对应USART3(不是UART4)
	// 根据VET6引脚配置.txt: ESP8266 TX→PB10(RXD), RX→PB11(TXD)
	usart3_init(baud);
}


void esp8266_send_at(char *str)
{
	/* 必须在发送前清空：先发再等 10ms 再清空会把模块已返回的 OK 删掉，
	 * 导致 esp8266_reset / self_test / connect_ap 等全部误判超时 */
	memset((void *)g_esp8266_rx_buf, 0, sizeof g_esp8266_rx_buf);
	g_esp8266_rx_cnt = 0;

	usart3_send_str(str);
}


void esp8266_send_bytes(uint8_t *buf,uint32_t len)
{
	usart3_send_bytes(buf,len);
}


void esp8266_send_str(char *buf)
{
	usart3_send_str(buf);
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
	uint32_t i;

	esp8266_send_at("AT+RST\r\n");
	/* 不少新版 AT 固件重启后只有 busy/OK/WIFI DISCONNECT，不再打印小写 ready，
	 * 若仍等待 ready 会导致 STM32 侧永远判失败；改为等待重启完成后轮询 AT */
	/* 含 WiFi 自动重连时模块可能较晚才响应 AT */
	delay_ms(3500);

	for (i = 0; i < 40; i++)
	{
		esp8266_send_at("AT\r\n");
		if (esp8266_find_str_in_rx_packet("OK", 800) == 0)
			return 0;
		delay_ms(300);
	}
	return -1;
}

// 注意：USART3_IRQHandler 已在 usart.c 中定义，此处不再重复定义
// usart.c 中的 USART3_IRQHandler 已经正确处理了 g_esp8266_rx_buf 的接收

