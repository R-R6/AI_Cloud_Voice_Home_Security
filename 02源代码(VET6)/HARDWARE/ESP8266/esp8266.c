#include "includes.h"

/*
 * 底层收发：USART3（硬件接 ESP8266 TX/RX，见 esp8266_init 注释）。
 * esp8266_send_at 在发送前清空接收缓冲，便于同步等待 OK；
 * 与 esp8266_mqtt.c 中依赖 g_esp8266_rx_cnt 的轮询方式不要混用同一缓冲区逻辑以免竞态。
 */

/**
 * @brief 通过 USART3 发送以 \\0 结尾的字符串（ESP8266 链路）。
 *
 * @param[in] str 待发送字符串指针。
 */
void usart3_send_str(char *str)
{
	usart_send_str(USART3,str);
}


/**
 * @brief 通过 USART3 发送指定长度字节流。
 *
 * @param[in] buf 数据首指针。
 * @param[in] len 字节长度。
 */
void usart3_send_bytes(uint8_t *buf,uint32_t len)
{
	usart_send_bytes(USART3,buf,len);
}


/**
 * @brief 初始化与 ESP8266 相连的 USART3（波特率可配，MQTT 工程常用 115200）。
 *
 * @param[in] baud 波特率数值（如 115200）。
 *
 * @note 硬件接线：ESP8266 UART 对应 PB10/PB11（USART3），勿与 UART4 混淆。
 */
void esp8266_init(uint32_t baud)
{
	// 注意: ESP8266硬件连接到PB10/PB11,这些引脚对应USART3(不是UART4)
	// 根据VET6引脚配置.txt: ESP8266 TX→PB10(RXD), RX→PB11(TXD)
	usart3_init(baud);
}


/**
 * @brief 发送一条 AT 文本前清空 g_esp8266_rx_buf / g_esp8266_rx_cnt，便于同步等待响应。
 *
 * @param[in] str 完整 AT 行（通常含 \\r\\n）。
 *
 * @note 与 esp8266_mqtt.c 中依赖接收缓冲未清空的轮询逻辑不同，二者勿混用于同一事务。
 */
void esp8266_send_at(char *str)
{
	/* 必须在发送前清空：先发再等 10ms 再清空会把模块已返回的 OK 删掉，
	 * 导致 esp8266_reset / self_test / connect_ap 等全部误判超时 */
	memset((void *)g_esp8266_rx_buf, 0, sizeof g_esp8266_rx_buf);
	g_esp8266_rx_cnt = 0;

	usart3_send_str(str);
}


/**
 * @brief 向 ESP8266 发送原始字节（不清空接收缓冲）。
 *
 * @param[in] buf 数据指针。
 * @param[in] len 长度。
 */
void esp8266_send_bytes(uint8_t *buf,uint32_t len)
{
	usart3_send_bytes(buf,len);
}


/**
 * @brief 向 ESP8266 发送以 \\0 结尾的字符串（不清空接收缓冲）。
 *
 * @param[in] buf 字符串指针。
 */
void esp8266_send_str(char *buf)
{
	usart3_send_str(buf);
}


/**
 * @brief 在 g_esp8266_rx_buf 中轮询查找子串，每 1 ms 递减超时计数。
 *
 * @param[in] str     期望出现的子串（如 "OK"）。
 * @param[in] timeout 最大等待毫秒数（与 delay_ms(1) 次数对应）。
 *
 * @retval 0  在超时前找到子串。
 * @retval -1 超时仍未找到。
 */
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


/**
 * @brief 发送 AT\\r\\n 并等待响应中出现 "OK"。
 *
 * @retval 0  模块响应正常。
 * @retval -1 超时未收到 OK。
 */
int32_t  esp8266_self_test(void)
{
	esp8266_send_at("AT\r\n");
	
	return esp8266_find_str_in_rx_packet("OK",1000);
}

/**
 * @brief 使用 AT+CWMODE_CUR 与 AT+CWJAP_CUR 连接指定 AP（分片发送 SSID/密码以降低栈占用）。
 *
 * @param[in] ssid WiFi 名称。
 * @param[in] pswd WiFi 密码。
 *
 * @retval 0  连接流程按代码路径成功结束。
 * @retval -1 STATION 模式或首段应答失败。
 * @retval -2 未在超时内收到期望的 OK/CONNECT 组合（SSID/密码错误、信道拥堵等）。
 *
 * @note 本工程 MQTT 路径主要使用 esp8266_mqtt_init 内的 AT+CWJAP；本函数保留作通用能力。
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



/**
 * @brief 发送 "+++" 序列并按规定间隔延时，退出 ESP8266 透传模式。
 *
 * @retval 0 固定返回 0（实际是否退出依赖模块状态与前后时序）。
 *
 * @note 前后各延时约 1 s，符合常见 AT 固件对 +++ 的要求。
 */
int32_t esp8266_exit_transparent_transmission (void)
{
	//退出透传模式，发送下一条AT指令要间隔1秒
	delay_ms ( 1000 ); 

	esp8266_send_at ("+++");
	
	//退出透传模式，发送下一条AT指令要间隔1秒
	delay_ms ( 1000 ); 

	return 0;
}

/**
 * @brief 打开 CIPMODE=1 并发送 AT+CIPSEND，进入透传发送态（等待 '>' 提示符）。
 *
 * @retval 0  成功进入。
 * @retval -1 CIPMODE 配置失败。
 * @retval -2 未收到发送提示符 '>'。
 */
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
 * @brief 使用 AT+CIPSTART 以 TCP/UDP 连接远端（分片发送以降低 sprintf 长串栈消耗）。
 *
 * @param[in] mode 协议名，如 "TCP" 或 "UDP"。
 * @param[in] ip   域名或 IP 字符串。
 * @param[in] port 端口号。
 *
 * @retval 0  在代码末尾路径返回 0 表示未命中错误分支（与 esp8266_find_str_in_rx_packet 逻辑配合）。
 * @retval -1 在约定超时内未检测到 CONNECT 与 OK 的特定组合（详见实现）。
 *
 * @note MQTT AT 路径不经过本函数；本函数适用于传统 CIP 透传/TCP 调试。
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

/**
 * @brief 发送 AT+CIPCLOSE 关闭当前 CIP 连接。
 *
 * @retval 0  未命中错误返回分支。
 * @retval -1 在超时内未同时检测到 CLOSED 与 OK（详见实现）。
 */
int32_t esp8266_disconnect_server(void)
{
	esp8266_send_at("AT+CIPCLOSE\r\n");
		
	if(esp8266_find_str_in_rx_packet("CLOSED",5000))
		if(esp8266_find_str_in_rx_packet("OK",5000))
			return -1;
	
	return 0;	
}


/**
 * @brief 配置 AT+CIPMUX，打开或关闭多连接模式。
 *
 * @param[in] b 0 或 1，传入 AT+CIPMUX 参数。
 *
 * @retval 0  设置成功（收到 OK）。
 * @retval -1 超时未收到 OK。
 */
int32_t esp8266_enable_multiple_id(uint32_t b)
{

	char buf[32]={0};
	
	sprintf(buf,"AT+CIPMUX=%d\r\n", b);
	esp8266_send_at(buf);
	
	if(esp8266_find_str_in_rx_packet("OK",5000))
		return -1;
	
	return 0;
}

/**
 * @brief 在模块上创建 TCP 服务器（AT+CIPSERVER=1,port）。
 *
 * @param[in] port 监听端口。
 *
 * @retval 0  成功。
 * @retval -1 超时未收到 OK。
 */
int32_t esp8266_create_server(uint16_t port)
{
	char buf[32]={0};
	
	sprintf(buf,"AT+CIPSERVER=1,%d\r\n", port);
	esp8266_send_at(buf);
	
	if(esp8266_find_str_in_rx_packet("OK",5000))
		return -1;
	
	return 0;
}

/**
 * @brief 关闭模块 TCP 服务器（AT+CIPSERVER=0,port）。
 *
 * @param[in] port 与创建时一致的端口参数（随 AT 固件版本可能仅 0 有效，以模块手册为准）。
 *
 * @retval 0  成功。
 * @retval -1 超时未收到 OK。
 */
int32_t esp8266_close_server(uint16_t port)
{
	char buf[32]={0};
	
	sprintf(buf,"AT+CIPSERVER=0,%d\r\n", port);
	esp8266_send_at(buf);
	
	if(esp8266_find_str_in_rx_packet("OK",5000))
		return -1;
	
	return 0;
}

/**
 * @brief 发送 ATE1/ATE0 打开或关闭 AT 回显。
 *
 * @param[in] b 非 0 打开回显；0 关闭回显。
 *
 * @retval 0  成功。
 * @retval -1 超时未收到 OK。
 */
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

/**
 * @brief 发送 AT+RST 复位模块，延时后轮询 AT 直至收到 OK。
 *
 * @retval 0  复位后模块响应 AT 正常。
 * @retval -1 多轮轮询后仍无 OK。
 *
 * @note 新版固件可能不再打印小写 ready；实现以轮询 AT 为准。
 */
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

