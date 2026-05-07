#include "includes.h"


// ==================== ESP8266 AT指令MQTT模式 ====================
// 注意：g_esp8266_rx_buf、g_esp8266_rx_cnt、g_esp8266_tx_buf 已在 usart.h 中定义，此处不再重复定义

// MQTT消息缓冲区
char  g_mqtt_msg[526];

// 本地接收计数缓存（用于判断接收完成）
static uint16_t g_esp8266_rx_cnt_pre = 0;

/* 长 AT 行（MQTTUSERCFG 等）放静态区，减轻 app_task_esp8266 栈压力 */
static char s_esp8266_cmd_buf[512];
/* MQTT 发布：转义后的 JSON、整行 AT（避免栈过大） */
static char s_mqtt_esc[1100];
static char s_mqtt_pub_line[1200];

/* ESP8266 单条 AT 输入长度上限约 256，超长则不要用 AT+MQTTPUB，直接 PUBRAW */
#define ESP8266_AT_LINE_SAFE_MAX 230

/* 不等 RX “静止”，随时扫描缓冲区（避免慢速/分包导致永远无 REV_OK） */
static int mqtt_rx_contains(const char *sub)
{
	static char scan[512];
	unsigned int n;

	n = (unsigned int)g_esp8266_rx_cnt;
	if (n == 0u)
		return 0;
	if (n >= sizeof(scan))
		n = (unsigned int)sizeof(scan) - 1u;
	memcpy(scan, (const void *)g_esp8266_rx_buf, n);
	scan[n] = '\0';
	return (strstr(scan, sub) != NULL);
}

#define REV_OK      0
#define REV_WAIT    1

/* AT 失败时观察是否收到任意字节：cnt=0 多为 PB10/11 未接到模块 TX 或接错到 UART4 */
static void esp8266_dbg_rx(const char *tag)
{
	unsigned int i, n = (unsigned int)g_esp8266_rx_cnt;

	dgb_printf_safe("esp8266_dbg[%s] rx_cnt=%lu\r\n", tag, (unsigned long)g_esp8266_rx_cnt);
	if (n > 48U)
		n = 48U;
	for (i = 0; i < n; i++)
		dgb_printf_safe(" %02X", (unsigned)g_esp8266_rx_buf[i]);
	if (n != 0U)
		dgb_printf_safe("\r\n");
}

// 清空接收缓冲区
void ESP8266_Clear(void)
{
	memset(g_esp8266_rx_buf, 0, sizeof(g_esp8266_rx_buf));
	g_esp8266_rx_cnt = 0;
	g_esp8266_rx_cnt_pre = 0;
}

// 等待接收完成（字节数在一段时间内不再增加则认为本帧结束）
// 注意：此处绝不能清空缓冲区，否则 ESP8266_SendCmd 里的 strstr 永远对着空缓冲
unsigned char ESP8266_WaitRecive(void)
{
	if(g_esp8266_rx_cnt == 0)
		return REV_WAIT;

	if(g_esp8266_rx_cnt == g_esp8266_rx_cnt_pre)
		return REV_OK;

	g_esp8266_rx_cnt_pre = (uint16_t)g_esp8266_rx_cnt;
	return REV_WAIT;
}

/* polls：循环次数，每次约 10ms，800≈8s；短超时用于 AT 探测便于打出进度日志 */
static unsigned char ESP8266_SendCmdPolls(char *cmd, char *res, unsigned int polls)
{
	unsigned int timeOut = polls;

	ESP8266_Clear();
	g_esp8266_rx_cnt_pre = 0;

	esp8266_send_bytes((uint8_t *)cmd, strlen(cmd));

	while(timeOut--)
	{
		if(ESP8266_WaitRecive() == REV_OK)
		{
			if(strstr((const char *)g_esp8266_rx_buf, res) != NULL)
			{
				ESP8266_Clear();
				g_esp8266_rx_cnt_pre = 0;
				return 0;
			}
			ESP8266_Clear();
			g_esp8266_rx_cnt_pre = 0;
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	return 1;
}

unsigned char ESP8266_SendCmd(char *cmd, char *res)
{
	return ESP8266_SendCmdPolls(cmd, res, 800U);
}

// MQTT无条件断开（使用AT指令）
void mqtt_disconnect()
{
	ESP8266_SendCmd("AT+MQTTCLEAN=0\r\n", "OK");
}

// 发送心跳包（使用AT指令）
void mqtt_send_heart(void)
{
	ESP8266_SendCmd("AT+MQTTPING\r\n", "OK");
}

// MQTT初始化（清理状态）
void mqtt_init(uint8_t *prx,uint16_t rxlen,uint8_t *ptx,uint16_t txlen)
{
	memset(g_esp8266_tx_buf,0,sizeof(g_esp8266_tx_buf)); //清空发送缓冲
	memset((void *)g_esp8266_rx_buf,0,sizeof(g_esp8266_rx_buf)); //清空接收缓冲

	// 无条件先主动断开
	mqtt_disconnect();
	delay_ms(100);
	
	mqtt_disconnect();
	delay_ms(100);
}

// MQTT连接服务器（使用AT指令方式 - OneNET新版物模型）
int32_t mqtt_connect(char *client_id,char *user_name,char *password)
{
	uint32_t cnt = 3;  // 重试3次
	uint32_t wait = 0;
	char cmd_buf[256];
	
	// 步骤1: 配置MQTT用户参数（鉴权信息）
	sprintf(cmd_buf, 
		"AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
		client_id, user_name, password);
	
	dgb_printf_safe("Step1: Config MQTT User...\r\n");
	while(cnt--)
	{
		if(ESP8266_SendCmd(cmd_buf, "OK") == 0)
		{
			dgb_printf_safe("MQTT User Config Success\r\n");
			break;
		}
		else
		{
			dgb_printf_safe("MQTT User Config Fail, Retrying...\r\n");
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
	}
	
	if(cnt == 0)
	{
		dgb_printf_safe("MQTT User Config Failed after 3 retries\r\n");
		return -1;
	}
	
	// 步骤2: 建立MQTT连接
	sprintf(cmd_buf, 
		"AT+MQTTCONN=0,\"%s\",%d,0\r\n",
		MQTT_BROKERADDRESS, MQTT_PORT);
	
	cnt = 3;  // 重试3次
	dgb_printf_safe("Step2: Connect to MQTT Broker...\r\n");
	while(cnt--)
	{
		if(ESP8266_SendCmd(cmd_buf, "OK") == 0)
		{
			dgb_printf_safe("MQTT Connect Success\r\n");
			return 0;  // 连接成功
		}
		else
		{
			dgb_printf_safe("MQTT Connect Fail, Retrying...\r\n");
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
	}
	
	dgb_printf_safe("MQTT Connect Failed after 3 retries\r\n");
	return -1;
}

// MQTT订阅/取消订阅主题（使用AT指令）
// topic: 主题
// qos: 消息等级 (0或1)
// whether: 1-订阅, 0-取消订阅
int32_t mqtt_subscribe_topic(char *topic,uint8_t qos,uint8_t whether)
{
	char cmd_buf[256];
	uint32_t cnt = 3;
	
	if(whether)
	{
		// 订阅主题
		sprintf(cmd_buf, 
			"AT+MQTTSUB=0,\"%s\",%d\r\n",
			topic, qos);
		
		dgb_printf_safe("Subscribe Topic: %s\r\n", topic);
		while(cnt--)
		{
			if(ESP8266_SendCmd(cmd_buf, "OK") == 0)
			{
				dgb_printf_safe("Subscribe Success\r\n");
				return 0;
			}
			else
			{
				dgb_printf_safe("Subscribe Fail, Retrying...\r\n");
				vTaskDelay(pdMS_TO_TICKS(1000));
			}
		}
	}
	else
	{
		// 取消订阅
		sprintf(cmd_buf, 
			"AT+MQTTUNSUB=0,\"%s\"\r\n",
			topic);
		
		dgb_printf_safe("Unsubscribe Topic: %s\r\n", topic);
		while(cnt--)
		{
			if(ESP8266_SendCmd(cmd_buf, "OK") == 0)
			{
				dgb_printf_safe("Unsubscribe Success\r\n");
				return 0;
			}
			else
			{
				dgb_printf_safe("Unsubscribe Fail, Retrying...\r\n");
				vTaskDelay(pdMS_TO_TICKS(1000));
			}
		}
	}
	
	return -1;
}

/* 把 JSON 放进 AT 的双引号字段：必须转义 " 与 \ */
static int mqtt_at_escape_payload(const char *src, char *dst, size_t dst_sz)
{
	size_t i, j = 0;

	for (i = 0; src[i] != '\0'; i++)
	{
		if (src[i] == '\\' || src[i] == '"')
		{
			if (j + 2 >= dst_sz)
				return -1;
			dst[j++] = '\\';
		}
		if (j + 1 >= dst_sz)
			return -1;
		dst[j++] = src[i];
	}
	if (j >= dst_sz)
		return -1;
	dst[j] = '\0';
	return (int)j;
}

/*
 * AT+MQTTPUBRAW：安信可 AT 2.2.x 常见两种行为
 * 1) 回 '>' 再发负载
 * 2) 仅回 OK（无 '>'），随后立即发指定长度原始字节
 */
static uint32_t mqtt_publish_pubraw(char *topic, char *message, uint8_t qos, unsigned int msg_len)
{
	char hdr[384];
	unsigned int hl;
	unsigned int polls;

	hl = (unsigned int)snprintf(hdr, sizeof(hdr),
		"AT+MQTTPUBRAW=0,\"%s\",%u,%u,0\r\n",
		topic, msg_len, (unsigned int)qos);
	if (hl <= 0u || hl >= sizeof(hdr))
		return 0u;

	ESP8266_Clear();
	g_esp8266_rx_cnt_pre = 0;
	esp8266_send_bytes((uint8_t *)hdr, hl);

	for (polls = 0u; polls < 600u; polls++)
	{
		if (mqtt_rx_contains("\r\nERROR") || mqtt_rx_contains("\nERROR"))
		{
			dgb_printf_safe("MQTT Publish Failed (PUBRAW cmd ERROR)\r\n");
			ESP8266_Clear();
			return 0u;
		}
		if (mqtt_rx_contains(">"))
			break;
		if (mqtt_rx_contains("OK\r\n") || mqtt_rx_contains("\r\nOK"))
			break;

		vTaskDelay(pdMS_TO_TICKS(10));
	}
	if (polls >= 600u)
	{
		dgb_printf_safe("MQTT Publish Failed (PUBRAW no OK/>)\r\n");
		ESP8266_Clear();
		return 0u;
	}

	ESP8266_Clear();
	g_esp8266_rx_cnt_pre = 0;
	vTaskDelay(pdMS_TO_TICKS(30));
	esp8266_send_bytes((uint8_t *)message, msg_len);

	for (polls = 0u; polls < 800u; polls++)
	{
		if (mqtt_rx_contains("\r\nERROR") || mqtt_rx_contains("\nERROR"))
			break;
		if (mqtt_rx_contains("OK\r\n") || mqtt_rx_contains("\r\nOK"))
		{
			ESP8266_Clear();
			g_esp8266_rx_cnt_pre = 0;
			return (uint32_t)msg_len;
		}

		vTaskDelay(pdMS_TO_TICKS(10));
	}

	dgb_printf_safe("MQTT Publish Failed (PUBRAW finish)\r\n");
	ESP8266_Clear();
	return 0u;
}

// MQTT发布数据（使用AT指令）
// topic: 主题
// message: 消息内容
// qos: 消息等级 (0或1)
//
// 1) 若整行 AT 不超过模块单行限制：AT+MQTTPUB + 转义 JSON（与 F103 示例同思路，避免 JSON 内引号截断）
// 2) 否则直接 AT+MQTTPUBRAW（长负载）
uint32_t mqtt_publish_data(char *topic, char *message, uint8_t qos)
{
	unsigned int msg_len;
	int n;

	if (topic == NULL || message == NULL)
		return 0;
	msg_len = (unsigned int)strlen(message);
	if (msg_len == 0u || msg_len > sizeof(g_mqtt_msg))
		return 0;

	if (mqtt_at_escape_payload(message, s_mqtt_esc, sizeof(s_mqtt_esc)) < 0)
		return mqtt_publish_pubraw(topic, message, qos, msg_len);

	n = snprintf(s_mqtt_pub_line, sizeof(s_mqtt_pub_line),
		"AT+MQTTPUB=0,\"%s\",\"%s\",%u,0\r\n",
		topic, s_mqtt_esc, (unsigned int)qos);
	if (n < 0 || (unsigned int)n >= sizeof(s_mqtt_pub_line))
		return mqtt_publish_pubraw(topic, message, qos, msg_len);

	/* 过长时发 AT+MQTTPUB 会被模块截断，表现为无 OK / 异常 */
	if ((unsigned int)n <= ESP8266_AT_LINE_SAFE_MAX)
	{
		if (ESP8266_SendCmd(s_mqtt_pub_line, "OK") == 0)
			return (uint32_t)msg_len;
	}

	return mqtt_publish_pubraw(topic, message, qos, msg_len);
}

// 设备状态上报（OneNET新版物模型格式）
void mqtt_report_devices_status(void)
{
	uint8_t led_1_sta = GPIO_ReadOutputDataBit(GPIOF,GPIO_Pin_9)  ? 0:1;
	uint8_t led_2_sta = GPIO_ReadOutputDataBit(GPIOF,GPIO_Pin_10) ? 0:1;
	uint8_t led_3_sta = GPIO_ReadOutputDataBit(GPIOE,GPIO_Pin_13) ? 0:1;

	// OneNET新版物模型JSON格式
	// 注意：每个属性值必须包裹在 {"value": xxx} 中
	// 不包含 method 和 version 字段
	sprintf(g_mqtt_msg,
		"{\"id\":\"123\",\"params\":{"
			"\"temperature\":{\"value\":%.1f},"
			"\"Humidity\":{\"value\":%.1f},"
			"\"switch_led_1\":{\"value\":%d},"
			"\"switch_led_2\":{\"value\":%d},"
			"\"switch_led_3\":{\"value\":%d},"
			"\"fire\":{\"value\":%d},"
			"\"mq2\":{\"value\":%d}"
		"}}",
		g_temp,
		g_humi,
		led_1_sta,
		led_2_sta,
		led_3_sta,
		g_fire_status,
		g_mq2_status);

	/* QoS 1 与参考示例一致；云端未建属性时仍可 PUBLISH，平台侧可能丢弃或记日志，与 AT 无关 */
	mqtt_publish_data(MQTT_PUBLISH_TOPIC, g_mqtt_msg, 1);
}

// ESP8266 MQTT初始化主函数
int32_t esp8266_mqtt_init(void)
{
	int32_t rt;
	char *cmd_buf = s_esp8266_cmd_buf;
	uint8_t retry;
	uint8_t wifi_ok;
	uint8_t mqtt_conn_ok;
	uint8_t usercfg_ok;

	esp8266_init(115200);

	rt = esp8266_exit_transparent_transmission();
	if (rt != 0)
		dgb_printf_safe("esp8266_exit_transparent_transmission fail\r\n");
	dgb_printf_safe("esp8266_exit_transparent_transmission success\r\n");
	dgb_printf_safe("esp8266: pause 2s then AT sync...\r\n");
	delay_ms(2000);

	/* 单次短超时(约1.2s)+每轮打印，避免误以为“没有日志” */
	for (retry = 0; retry < 40; retry++)
	{
		dgb_printf_safe("esp8266 AT try %u\r\n", (unsigned)retry);
		if (ESP8266_SendCmdPolls("AT\r\n", "OK", 120U) == 0)
			break;
		if (((retry + 1U) % 10U) == 0U)
			esp8266_dbg_rx("AT no OK");
		vTaskDelay(pdMS_TO_TICKS(300));
	}
	if (retry >= 40)
	{
		esp8266_dbg_rx("AT sync final");
		dgb_printf_safe("esp8266 AT sync fail (查:PB10/11与模块RX/TX交叉、共地;勿接PC10/11 UART4)\r\n");
		return -2;
	}
	dgb_printf_safe("esp8266 AT sync OK\r\n");

	if (ESP8266_SendCmdPolls("ATE0\r\n", "OK", 200U) != 0)
	{
		dgb_printf_safe("esp8266_enable_echo(ATE0) fail\r\n");
		return -3;
	}
	dgb_printf_safe("esp8266_enable_echo(0) success\r\n");
	delay_ms(200);

	for (retry = 0; retry < 10; retry++)
	{
		dgb_printf_safe("esp8266 CWMODE try %u\r\n", (unsigned)retry);
		if (ESP8266_SendCmdPolls("AT+CWMODE=1\r\n", "OK", 200U) == 0)
			break;
		vTaskDelay(pdMS_TO_TICKS(500));
	}
	if (retry >= 10)
	{
		dgb_printf_safe("esp8266 CWMODE fail\r\n");
		return -4;
	}

	if (ESP8266_SendCmdPolls("AT+CWDHCP=1,1\r\n", "OK", 200U) != 0)
		dgb_printf_safe("esp8266 CWDHCP warn (ignored)\r\n");

	sprintf(cmd_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
	wifi_ok = 0;
	for (retry = 0; retry < 60; retry++)
	{
		dgb_printf_safe("esp8266 CWJAP try %u\r\n", (unsigned)retry);
		if (ESP8266_SendCmdPolls(cmd_buf, "GOT IP", 1200U) == 0)
		{
			wifi_ok = 1;
			break;
		}
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
	if (!wifi_ok)
	{
		dgb_printf_safe("esp8266 CWJAP / GOT IP fail\r\n");
		return -5;
	}
	dgb_printf_safe("esp8266 WiFi GOT IP\r\n");
	/* 协议栈稳定；仅复位 MCU 时模块侧 MQTT  session 可能仍在，必须先清理 */
	delay_ms(800);
	for (retry = 0; retry < 3; retry++)
	{
		ESP8266_SendCmdPolls("AT+MQTTCLEAN=0\r\n", "OK", 200U);
		vTaskDelay(pdMS_TO_TICKS(400));
	}
	delay_ms(300);

	sprintf(cmd_buf,
		"AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
		MQTT_CLIENTID, MQTT_USARNAME, MQTT_PASSWD);

	dgb_printf_safe("Config MQTT User...\r\n");
	usercfg_ok = 0;
	for (retry = 0; retry < 8; retry++)
	{
		dgb_printf_safe("MQTT User Config try %u\r\n", (unsigned)retry);
		if (ESP8266_SendCmd(cmd_buf, "OK") == 0)
		{
			usercfg_ok = 1;
			break;
		}
		vTaskDelay(pdMS_TO_TICKS(600));
	}
	if (!usercfg_ok)
	{
		esp8266_dbg_rx("MQTTUSERCFG");
		dgb_printf_safe("MQTT User Config Fail\r\n");
		return -6;
	}
	dgb_printf_safe("MQTT User Config Success\r\n");
	delay_ms(1000);

	sprintf(cmd_buf,
		"AT+MQTTCONN=0,\"%s\",%d,0\r\n",
		MQTT_BROKERADDRESS, MQTT_PORT);

	mqtt_conn_ok = 0;
	dgb_printf_safe("Connect to MQTT Broker...\r\n");
	for (retry = 0; retry < 3; retry++)
	{
		if(ESP8266_SendCmd(cmd_buf, "OK") == 0)
		{
			dgb_printf_safe("MQTT Connect Success\r\n");
			mqtt_conn_ok = 1;
			break;
		}
		dgb_printf_safe("MQTT Connect Fail, Retrying (%u)...\r\n", (unsigned)(retry + 1));
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	if(!mqtt_conn_ok)
	{
		dgb_printf_safe("MQTT Connect Failed after 3 retries\r\n");
		return -7;
	}
	
	delay_ms(1000);
	
	// 步骤8: 订阅云端下发指令主题
	if(mqtt_subscribe_topic(MQTT_SUBSCRIBE_TOPIC, 0, 1) != 0)
	{
		dgb_printf_safe("mqtt_subscribe_topic fail\r\n");
		return -8;
	}	
	dgb_printf_safe("mqtt_subscribe_topic success\r\n");
	
	// 步骤9: （可选）订阅上报回执主题
	if(mqtt_subscribe_topic(MQTT_REPLY_TOPIC, 0, 1) != 0)
	{
		dgb_printf_safe("Subscribe reply topic fail (optional)\r\n");
		// 不返回错误，这是可选的
	}
	
	dgb_printf_safe("===== ESP8266 MQTT Init Complete =====\r\n");
	return 0;
}
